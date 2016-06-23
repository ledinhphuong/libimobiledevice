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
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/xcuitest.h>
#include <libimobiledevice/installation_proxy.h>
#include <plist/plist.h>

#ifdef WIN32
#include <windows.h>
#define sleep(x) Sleep(x*1000)
#endif

// typedef enum {
// 	SCREENSHOTR_E_SUCCESS       =  0,
// 	SCREENSHOTR_E_INVALID_ARG   = -1,
// 	SCREENSHOTR_E_PLIST_ERROR   = -2,
// 	SCREENSHOTR_E_MUX_ERROR     = -3,
// 	SCREENSHOTR_E_BAD_VERSION   = -4,
// 	SCREENSHOTR_E_UNKNOWN_ERROR = -256
// } screenshotr_error_t;
// struct screenshotr_client_private {
// 	device_link_service_client_t parent;
// };
// typedef screenshotr_client_private *screenshotr_client_t;

static int quit_flag = 0;

/**
* signal handler function for cleaning up properly
*/
static void on_signal(int sig) {
  fprintf(stderr, "Exiting...\n");
  quit_flag++;
}

// static screenshotr_error_t screenshotr_error(device_link_service_error_t err)
// {
// 	switch (err) {
// 		case DEVICE_LINK_SERVICE_E_SUCCESS:
// 			return SCREENSHOTR_E_SUCCESS;
// 		case DEVICE_LINK_SERVICE_E_INVALID_ARG:
// 			return SCREENSHOTR_E_INVALID_ARG;
// 		case DEVICE_LINK_SERVICE_E_PLIST_ERROR:
// 			return SCREENSHOTR_E_PLIST_ERROR;
// 		case DEVICE_LINK_SERVICE_E_MUX_ERROR:
// 			return SCREENSHOTR_E_MUX_ERROR;
// 		case DEVICE_LINK_SERVICE_E_BAD_VERSION:
// 			return SCREENSHOTR_E_BAD_VERSION;
// 		default:
// 			break;
// 	}
// 	return SCREENSHOTR_E_UNKNOWN_ERROR;
// }
//
// screenshotr_error_t instruments_client_new(idevice_t device, lockdownd_service_descriptor_t service,
// 					   screenshotr_client_t * client)
// {
// 	if (!device || !service || service->port == 0 || !client || *client)
// 		return SCREENSHOTR_E_INVALID_ARG;
//
// 	device_link_service_client_t dlclient = NULL;
// 	screenshotr_error_t ret = screenshotr_error(device_link_service_client_new(device, service, &dlclient));
// 	if (ret != SCREENSHOTR_E_SUCCESS) {
// 		return ret;
// 	}
//
// 	screenshotr_client_t client_loc = (screenshotr_client_t) malloc(sizeof(struct screenshotr_client_private));
// 	client_loc->parent = dlclient;
//
// 	/* perform handshake */
// 	ret = screenshotr_error(device_link_service_version_exchange(dlclient, SCREENSHOTR_VERSION_INT1, SCREENSHOTR_VERSION_INT2));
// 	if (ret != SCREENSHOTR_E_SUCCESS) {
// 		debug_info("version exchange failed, error %d", ret);
// 		screenshotr_client_free(client_loc);
// 		return ret;
// 	}
//
// 	*client = client_loc;
//
// 	return ret;
// }

void print_usage(int argc, char **argv);
int main(int argc, char **argv) {
	signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

	int result = -1;
	int i;
	const char *udid = NULL;
	const char *bundle = NULL;
	const char *session = NULL;

  char *appPath = NULL;
  idevice_t device = NULL;
  lockdownd_error_t ldret = LOCKDOWN_E_UNKNOWN_ERROR;
  instproxy_client_t instproxy_client = NULL;
  lockdownd_client_t lckd = NULL;
  lockdownd_service_descriptor_t service = NULL;
  // screenshotr_client_t shotr = NULL;

	/* parse cmdline args */
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
		else {
			print_usage(argc, argv);
			return 0;
		}
	}

  if (!udid || !bundle || !session) {
    print_usage(argc, argv);
    return 0;
  }

  // connect to device
  if (IDEVICE_E_SUCCESS != idevice_new(&device, udid)) {
		if (udid) {
			printf("No device found with udid %s, is it plugged in?\n", udid);
		} else {
			printf("No device found, is it plugged in?\n");
		}
		goto cleanup;
	}

  // install app to device
  // - use ideviceinstaller

  // launch/relaunch app and get pid
  if (instproxy_client_start_service(device, &instproxy_client, "idevicerun") != INSTPROXY_E_SUCCESS) {
    fprintf(stderr, "Could not start installation proxy service.\n");
    goto cleanup;
  }

  instproxy_client_get_path_for_bundle_identifier(instproxy_client, bundle, &appPath);
  printf("appPath=%s\n", appPath);
  instproxy_client_free(instproxy_client);
  instproxy_client = NULL;

  // connect to com.apple.instruments.remoteserver
  if (LOCKDOWN_E_SUCCESS != (ldret = lockdownd_client_new_with_handshake(device, &lckd, NULL))) {
		idevice_free(device);
		printf("ERROR: Could not connect to lockdownd, error code %d\n", ldret);
		goto cleanup;
	}

  lockdownd_start_service(lckd, "com.apple.instruments.remoteserver", &service);
	lockdownd_client_free(lckd);
	if (service && service->port > 0) {
    printf("com.apple.instruments.remoteserver is started at port=%d\n", service->port);

    // if (screenshotr_client_new(device, service, &shotr) != SCREENSHOTR_E_SUCCESS) {
    //   printf("Could not connect to instruments!\n");
    // }
  }


  // create DVTDevice


  // create DTXTransport


  // create DTXProxyChannel


  // create XCTestManager_DaemonConnectionInterface


  // create DTXRemoteInvocationReceipt


  // wait for exit signal
  printf("Entering run loop...\n");
  while (!quit_flag) {
    printf("Running\n");
		sleep(1);
  }

cleanup:
  // if (shotr) screenshotr_client_free(shotr);
  if (service) lockdownd_service_descriptor_free(service);
  if (device) idevice_free(device);
  if (appPath) free(appPath);
	printf("Finish.\n");

	return result;
}

void print_usage(int argc, char **argv)
{
	char *name = NULL;

	name = strrchr(argv[0], '/');
	printf("Usage: %s OPTIONS\n", (name ? name + 1: argv[0]));
	printf("Run Xcode UI tests on real device.\n");
	printf("  -d, --debug\t\tEnable communication debugging\n");
	printf("  -u, --udid UDID\tTarget specific device by its 40-digit device UDID\n");
	printf("  -r, --run PATH\tRun (debug) app specified by on-device path (use ideviceinstaller -l -o xml to find it).\n");
	printf("  -b, --bundle\t\tBundle ID for app to start.\n");
	printf("  -s, --session\t\tSession ID of test bundle.\n");
	printf("  -h, --help\t\tPrints usage information\n");
	printf("\n");
	printf("Homepage: <" PACKAGE_URL ">\n");
}
