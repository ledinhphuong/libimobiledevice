#ifndef KOBISCREENSHOT_H
#define KOBISCREENSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>

typedef struct DTXRemoteInvocationReceipt DTXRemoteInvocationReceipt;
typedef DTXRemoteInvocationReceipt *DTXRemoteInvocationReceipt_t;

typedef struct DVTDevice DVTDevice;
typedef DVTDevice *DVTDevice_t;

/**
 * Get a screen shot from the connected device.
 *
 * @param client The connection screenshotr service client.
 * @param imgdata Pointer that will point to a newly allocated buffer
 *     containing TIFF image data upon successful return. It is up to the
 *     caller to free the memory.
 * @param imgsize Pointer to a uint64_t that will be set to the size of the
 *     buffer imgdata points to upon successful return.
 *
 * @return SCREENSHOTR_E_SUCCESS on success, SCREENSHOTR_E_INVALID_ARG if
 *     one or more parameters are invalid, or another error code if an
 *     error occured.
 */
DTXRemoteInvocationReceipt_t _IDE_initiateSessionWithIdentifier(idevice_t device, const char* client);

#ifdef __cplusplus
}
#endif

#endif
