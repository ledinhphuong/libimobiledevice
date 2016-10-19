#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/kobiscreenshot.h>

#include "common/utils.h"
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/screenshotr.h>

#ifdef WIN32
  #include <windows.h>
  #include <winsock2.h>
  #define SHUT_RD SD_READ
  #define SHUT_WR SD_WRITE
  #define SHUT_RDWR SD_BOTH
  #define sleep(x) Sleep(x*1000)
#else
  #include <sys/socket.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <sys/un.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
#endif

static int serverId = 0;
static int clientId = 0;

static int nQuitFlag = 0;
static void onSignal(int sig) {
  fprintf(stderr, "Exiting...\n");
  nQuitFlag++;

  if (clientId > 0) close(clientId);
  if (serverId > 0) close(serverId);

  clientId = 0;
  serverId = 0;
}

char* getProcessName(int argc, char **argv) {
  char *name = strrchr(argv[0], '/');
  return name ? name + 1 : argv[0];
}

void printUsage(int argc, char **argv) {
	printf("Usage: %s [OPTIONS]\n", getProcessName(argc, argv));
	printf("Gets screenshot stream from a device.\n");
	printf("NOTE: A mounted developer disk image is required on the device, otherwise\n");
	printf("the screenshotr service is not available.\n\n");
	printf("  -d, --debug\t\tenable communication debugging\n");
	printf("  -u, --udid UDID\ttarget specific device by its 40-digit device UDID\n");
  printf("  -p, --port PORT\tport on local machine\n");
	printf("  -h, --help\t\tprints usage information\n");
	printf("\n");
}

static inline void putUInt32LE(char* data, int value) {
  data[0] = (value & 0x000000FF) >> 0;
  data[1] = (value & 0x0000FF00) >> 8;
  data[2] = (value & 0x00FF0000) >> 16;
  data[3] = (value & 0xFF000000) >> 24;
}

int main(int argc, char **argv) {
  signal(SIGINT, onSignal);
  signal(SIGTERM, onSignal);

	idevice_t device = NULL;
  lockdownd_client_t lckd = NULL;
  lockdownd_error_t ldret = LOCKDOWN_E_UNKNOWN_ERROR;
  lockdownd_service_descriptor_t service = NULL;
	screenshotr_client_t shotr = NULL;
	const char *udid = NULL;
  uint16_t port = 0;

  struct sockaddr_in serverAddress;
  uint64_t imageLengthSize = 4;
  uint64_t bufferSize = 1024*1024*2; // 2 mb
  char *buffer = malloc(bufferSize);


	// Parse cmdline args
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")) {
			idevice_set_debug_level(1);
			continue;
		}
		else if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--udid")) {
			i++;
			if (!argv[i] || (strlen(argv[i]) != 40)) {
				printUsage(argc, argv);
				return 0;
			}
			udid = argv[i];
			continue;
		}
    else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port")) {
			i++;
			if (!argv[i] || atoi(argv[i]) < 1) {
				printUsage(argc, argv);
				return 0;
			}
			port = atoi(argv[i]);
			continue;
		}
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printUsage(argc, argv);
			return 0;
		}
		else {
			printUsage(argc, argv);
			return 0;
		}
	}

  if (udid == NULL || port < 1) {
    printUsage(argc, argv);
    return 0;
  }


  // Start socket server
  serverId = socket(AF_INET, SOCK_STREAM, 0);

  memset(&serverAddress, '0', sizeof(serverAddress));
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddress.sin_port = htons(port);
  bind(serverId, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

  listen(serverId, 1);
  printf("%s is listening at port: %d\n", getProcessName(argc, argv), port);

  while (!nQuitFlag) {
    clientId = accept(serverId, (struct sockaddr*)NULL, NULL);
    if (clientId < 1) {
      fprintf(stderr, "No client connection\n");
      sleep(1);
      continue;
    }

    printf("Got a connection %d\n", clientId);

    // Handshake with device
    if (IDEVICE_E_SUCCESS != idevice_new(&device, udid)) {
      if (udid) {
        fprintf(stderr, "No device found with udid %s, is it plugged in?\n", udid);
      }
      else {
        fprintf(stderr, "No device found, is it plugged in?\n");
      }
      goto clean_up;
    }

    if (LOCKDOWN_E_SUCCESS != (ldret = lockdownd_client_new_with_handshake(device, &lckd, NULL))) {
      fprintf(stderr, "Could not connect to lockdownd, error code %d\n", ldret);
      goto clean_up;
    }

    // Start screenshot service once time
    lockdownd_start_service(lckd, "com.apple.mobile.screenshotr", &service);
    lockdownd_client_free(lckd);
    if (!service || service->port < 1) {
      fprintf(stderr, "Could not start screenshotr service! Remember that you have to mount the Developer disk image on your device if you want to use the screenshotr service.\n");
      goto clean_up;
    }

    if (screenshotr_client_new(device, service, &shotr) != SCREENSHOTR_E_SUCCESS) {
      fprintf(stderr, "Cound not connect to screenshotr\n");
      goto clean_up;
    }

    printf("Taking screenshot and send back to client via socket...\n");

    // Capture screenshot
    char *imageData = NULL;
    uint64_t imageSize = 0;
    while (!nQuitFlag) {
      free(imageData);
      imageSize = 0;

      // clock_t begin = clock();
      int ret = screenshotr_take_screenshot(shotr, &imageData, &imageSize) == SCREENSHOTR_E_SUCCESS;
      // printf("Screenshot taken: %.0f millisecs\n", (double)((clock()-begin)*1000/CLOCKS_PER_SEC));

      if (ret) {
        if (bufferSize < imageSize + imageLengthSize) {
          uint64_t delta = 1024;
          bufferSize = imageSize + imageLengthSize + delta;
          buffer = realloc(buffer, bufferSize);
        }

        // Prepare binary to send via socket
        putUInt32LE(buffer, imageSize);
        memcpy(buffer + imageLengthSize, imageData, imageSize);

        int sentSz = 0;
        if ((sentSz = write(clientId, buffer, imageSize + imageLengthSize)) < 0) {
          printf("Cannot send frame\n");
          close(clientId);

          free(imageData);
          imageSize = 0;
          clientId = 0;
          break;
        }
        else {
          time_t ltime = time(NULL);
          printf("%s Sent frame: %d\n", asctime(localtime(&ltime)), sentSz);
        }
      }
    }
  }

clean_up:
  printf("Cleaning up...\n");
  // Socket stuffs
  if (clientId > 0) close(clientId);
  if (serverId > 0) close(serverId);
  free(buffer);

  // Lockdown stuff
  shotr && screenshotr_client_free(shotr);
	service && lockdownd_service_descriptor_free(service);
	device && idevice_free(device);

	return 0;
}
