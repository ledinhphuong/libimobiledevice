#ifndef __XCUITEST_H
#define __XCUITEST_H

#include <stdbool.h>
#include "libimobiledevice/xcuitest.h"
#include "device_link_service.h"

struct DTXRemoteInvocationReceipt {
	void *_guard;
	void *_completionHandler;
	int _returnValue;
	unsigned int _returnType;
};

struct DVTDevice {
	void *_instrumentsServerMessageQueue;
  void *_capabilities;
  void *_appExtensionInstallObserverChannel;
  void *_appListingChannelQueue;
  bool _ignored;
  bool _usedForDevelopment;
  bool _canSelectArchitectureToExecute;
  bool _available;
  void *_extension;
  void *_deviceLocation;
  char *_nativeArchitecture;
  char *_operatingSystemVersionWithBuildNumber;
  char *_modelUTI;
  char *_modelName;
  void *_deviceType;
  void *_supportedArchitectures;
  char *_name;
  char *_modelCode;
  void *_platform;
  char *_operatingSystemVersion;
  char *_operatingSystemBuild;
  char *_identifier;
};

#endif
