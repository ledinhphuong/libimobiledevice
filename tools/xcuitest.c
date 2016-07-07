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
#include <libimobiledevice/xcuitest.h>

#include "common/utils.h"
#include "common/socket.h"

#ifdef WIN32
#include <windows.h>
#define sleep(x) Sleep(x*1000)
#endif

static int quit_flag = 0;
static int testmanagerdSocket = -1;

/**
 * signal handler function for cleaning up properly
 */
static void on_signal(int sig) {
  fprintf(stderr, "Exiting...\n");
  quit_flag++;
  if (testmanagerdSocket > 0) {
    socket_close(testmanagerdSocket);
    testmanagerdSocket = -1;
  }
}

void print_usage(int argc, char **argv) {
  char *name = NULL;

  name = strrchr(argv[0], '/');
  printf("Usage: %s [OPTIONS] PORT\n", (name ? name + 1: argv[0]));
  printf("Run Xcode UI tests on real device.\n");
  printf("  -d, --debug\t\tEnable communication debugging\n");
  printf("  -u, --udid UDID\tTarget specific device by its 40-digit device UDID\n");
  printf("  -r, --run PATH\tRun (debug) app specified by on-device path (use ideviceinstaller -l -o xml to find it).\n");
  printf("  -b, --bundle\t\tBundle ID for app to start.\n");
  printf("  -s, --session\t\tSession ID of test bundle.\n");
  printf("  -h, --help\t\tPrints usage information\n");
  printf("  PORT, \t\tPort of testmanagerd proxy\n");
  printf("\n");
  printf("Homepage: <" PACKAGE_URL ">\n");
}

int main(int argc, char **argv) {
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  int result = -1;
  int i;

  const char *udid = NULL;
  const char *bundle = NULL;
  const char *session = NULL;
  char *appPath = NULL;

  const char *testmanagerdAddr = "localhost";
  int testmanagerdPort = 0;
  int plistMaxSz = 1024*1024; // 1mb
  char *plistBuffer = NULL;
  char *chunk = NULL;

  // parse cmdline args
  for (i = 1; i < argc; i++) {
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
    else if (!strcmp(argv[i], "-b") || !strcmp(argv[i], "--bundle")) {
      i++;
      if (!argv[i]) {
        print_usage(argc, argv);
        return 0;
      }
      bundle = argv[i];
      continue;
    }
    else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--session")) {
      i++;
      if (!argv[i]) {
        print_usage(argc, argv);
        return 0;
      }
      session = argv[i];
      continue;
    }
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      print_usage(argc, argv);
      return 0;
    }
    else if (atoi(argv[i]) > 0) {
      testmanagerdPort = atoi(argv[i]);
      continue;
    }
    else {
      print_usage(argc, argv);
      return 0;
    }
  }

  // PORT is mandatory
  if (!testmanagerdPort) {
    fprintf(stderr, "Please specify a PORT.\n");
    print_usage(argc, argv);
    goto cleanup;
  }

  // connect to testmanagerd proxy
  if ((testmanagerdSocket = socket_connect(testmanagerdAddr, testmanagerdPort)) <= 0) {
    printf("Cannot establish tcp connection to testmanagerd\n");
    return -1;
  }

  printf("Connected to testmanagerd proxy at %d\n", testmanagerdPort);

  // repaire memory for plist
  // plistBuffer = malloc(plistMaxSz);
  // memset(plistBuffer, 0, plistMaxSz);

  // convert plist binary to xml
  // const char *in = "/Users/phuongdle/MyProjects/org/libimobiledevice/2.bin";
  // const char *out = "/Users/phuongdle/MyProjects/org/libimobiledevice/out.xml";
  // plist_t plist = NULL;
  // plist_read_from_filename(plist, in);//plist_new_dict();
  // if (plist)
  //   printf("Can read plist\n");
  // else
  //   printf("Cannot read plist\n");
  // remove(out);
  // plist_write_to_filename(plist, out, PLIST_FORMAT_XML);
  // plist_free(plist);

  // create simple plist file
  // plist_t plist = plist_new_dict();
  // if (plist)
  //   printf("Created plist\n");
  // else
  //   printf("Cannot create plist\n");
  // plist_dict_set_item(plist, "GUID", plist_new_string("---"));
  // plist_dict_set_item(plist, "iTunes Version", plist_new_string("10.0.1"));
  //
  // remove(out2);
  // plist_write_to_filename(plist, out2, PLIST_FORMAT_XML);
  // plist_free(plist);
  ////////////////////

  int chunkSz = 1024*1024;
  chunk = malloc(chunkSz);
  memset(chunk, 0, chunkSz);

  // wait for exit signal
  int n = 0;
  while (!quit_flag) {
    // sleep(1);

    // receive chunk
    int readSize = 0;
    memset(chunk, 0, chunkSz);
    if ((readSize = read(testmanagerdSocket, chunk, chunkSz-1)) > 0) {
      printf(">>>>>>>> chunk size: %d\n", readSize);
      chunk[readSize] = '\0';

      fwrite(chunk, readSize, 1, stdout);
      printf("\n");

      // parse plist package
      // 32-bit big endian
      uint32_t plistLen = 0;

      //https://www.theiphonewiki.com/wiki/Usbmux
      if (readSize > 4) {
        plistLen = (chunk[3]<<24)|(chunk[2]<<16)|(chunk[1]<<8)|chunk[0];
        // memcpy(&plistLen, chunk, 4);
        printf("plistLen: %d\n", plistLen);

        // if (plistLen <= readSize-4) {
        //   printf("Parsing plist...\n");
        //
        //   plist_t dict = NULL;
        //   plist_from_bin(chunk+4, plistLen, dict);
        //
        //   if (dict) {
        //     printf("Got plist\n");
        //
        //     char *version = NULL;
        //   	plist_t node = plist_dict_get_item(dict, "version");
        //   	if (node && (plist_get_node_type(node) == PLIST_STRING)) {
        //   		plist_get_string_val(node, &version);
        //       printf(">>>> version: %s\n", version);
        //   	}
        //
        //     plist_free(dict);
        //   }
        // }
      }
    }

    //
    //   char name[8];
    //   memset(name, 0, 8);
    //   sprintf(name, "%d.bin", ++n);
    //   char *path = string_build_path("/Users/phuongdle/MyProjects/org/libimobiledevice", name, NULL);
    //   remove(path);
    //   buffer_write_to_filename(path, chunk, readSize);
    //   free(path);
    //
    //
    // }
    // else {
    //   sleep(1);
    // }
  }

cleanup:
  if (plistBuffer) free(plistBuffer);
  if (chunk) free(chunk);
  if (appPath) free(appPath);
  printf("Stopped.\n");

  return result;
}
