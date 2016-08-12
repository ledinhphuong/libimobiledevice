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

static int server_fd = 0;
static int client_fd = 0;

static int quit_flag = 0;
static void on_signal(int sig) {
  fprintf(stderr, "Exiting...\n");
  quit_flag++;

  if (client_fd > 0) close(client_fd);
  if (server_fd > 0) close(server_fd);

  client_fd = 0;
  server_fd = 0;
}

void print_usage(int argc, char **argv) {
	char *name = NULL;

	name = strrchr(argv[0], '/');
	printf("Usage: %s [OPTIONS]\n", (name ? name + 1 : argv[0]));
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
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

	idevice_t device = NULL;
	lockdownd_client_t lckd = NULL;
	lockdownd_error_t ldret = LOCKDOWN_E_UNKNOWN_ERROR;
	screenshotr_client_t shotr = NULL;
	lockdownd_service_descriptor_t service = NULL;
	const char *udid = NULL;
  uint16_t port = 0;

  struct sockaddr_in serv_addr;
  uint64_t lenSz = 4;
  uint64_t buffSz = 1024*1024*2; // 2 mb
  char *buff = malloc(buffSz);


	/* parse cmdline args */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")) {
			idevice_set_debug_level(1);
			continue;
		}
		else if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--udid")) {
			i++;
			if (!argv[i] || (strlen(argv[i]) != 40)) {
				print_usage(argc, argv);
				return 0;
			}
			udid = argv[i];
			continue;
		}
    else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port")) {
			i++;
			if (!argv[i] || atoi(argv[i]) < 1) {
				print_usage(argc, argv);
				return 0;
			}
			port = atoi(argv[i]);
			continue;
		}
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			print_usage(argc, argv);
			return 0;
		}
		else {
			print_usage(argc, argv);
			return 0;
		}
	}

  if (udid == NULL || port < 1) {
    print_usage(argc, argv);
    return 0;
  }


  // start socket server
  server_fd = socket(AF_INET, SOCK_STREAM, 0);

  memset(&serv_addr, '0', sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);
  bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

  listen(server_fd, 1);
  // TODO make get name func
  char *name = NULL;
	name = strrchr(argv[0], '/');
  printf("%s's listening at port: %d\n", name ? name + 1 : argv[0], port);

  while (!quit_flag && (client_fd = accept(server_fd, (struct sockaddr*)NULL, NULL)) > 0) {
    fprintf(stderr, "Got a connection %d\n", client_fd);

    // handshake with device
    if (IDEVICE_E_SUCCESS != idevice_new(&device, udid)) {
      if (udid) {
        printf("No device found with udid %s, is it plugged in?\n", udid);
      }
      else {
        printf("No device found, is it plugged in?\n");
      }
      goto clean_up;
    }

    if (LOCKDOWN_E_SUCCESS != (ldret = lockdownd_client_new_with_handshake(device, &lckd, NULL))) {
      printf("ERROR: Could not connect to lockdownd, error code %d\n", ldret);
      goto clean_up;
    }

    // start screenshot service once time
    if (!service) {
      lockdownd_start_service(lckd, "com.apple.mobile.screenshotr", &service);
      lockdownd_client_free(lckd);
      if (service && service->port < 1) goto clean_up;
      if (screenshotr_client_new(device, service, &shotr) != SCREENSHOTR_E_SUCCESS) goto clean_up;
    }

    // capture screenshot
    char *imgData = NULL;
    uint64_t imgSz = 0;
    while (!quit_flag) {
      free(imgData);
      imgSz = 0;

      clock_t begin = clock();
      int ret = screenshotr_take_screenshot(shotr, &imgData, &imgSz) == SCREENSHOTR_E_SUCCESS;
      printf("Screenshot taken: %.0f millisecs\n", (double)((clock()-begin)*1000/CLOCKS_PER_SEC));

      if (ret) {
        if (buffSz < imgSz + lenSz) {
          uint64_t delta = 1024;
          buffSz = imgSz + lenSz + delta;
          buff = realloc(buff, buffSz);
        }

        // prepare binary to send via socket
        putUInt32LE(buff, imgSz);
        memcpy(buff + lenSz, imgData, imgSz);

        if (write(client_fd, buff, imgSz + lenSz) < 0) {
          close(client_fd);

          free(imgData);
          imgSz = 0;
          client_fd = 0;
          break;
        }
      }
    }
  }

clean_up:
  // socket stuffs
  if (client_fd > 0) close(client_fd);
  if (server_fd > 0) close(server_fd);
  free(buff);

  // lockdown stuff
  shotr && screenshotr_client_free(shotr);
	service && lockdownd_service_descriptor_free(service);
	device && idevice_free(device);

	return 0;
}
