#include <stdio.h>
#include <stdlib.h>

#include <libusb.h>

#include "../http/http.h"
#include "usb.h"

#define USB_CONTEXT NULL

usb_sock_t *open_usb()
{
	libusb_context *context;
	int status = 1;
	status = libusb_init(&context);
	if (status < 0) {
		ERR("libusb init failed with error code %d", status);
		goto error;
	}

	libusb_device **device_list = NULL;
	ssize_t device_count = libusb_get_device_list(context, &device_list);
	if (device_count < 0) {
		ERR("failed to get list of usb devices");
		goto error;
	}


	libusb_device *printer_device = NULL;
	for (ssize_t i = 0; i < device_count; i++) {
		libusb_device *candidate = device_list[i];
		struct libusb_device_descriptor desc;
		libusb_get_device_descriptor(candidate, &desc);
		// TODO: error if device is not printer supporting IPP
		if (desc.idVendor == 0x03f0 &&
			desc.idProduct == 0xc511
			// TODO: filter on serial
			) {
			printer_device = candidate;
			break;
		}
	}

	if (printer_device == NULL) {
		ERR("no printer found by that vid & pid & serial");
		goto error;
	}

	// TODO: if kernel claimed device ask kernel to let it go

	libusb_device_handle *printer_handle;
	status = libusb_open(printer_device, &printer_handle);
	if (status != 0) {
		ERR("failed to open device");
		goto error;
	}


	// TODO: close printer with libusb_close()

error:
	if (device_list != NULL) {
		for (ssize_t i = 0; i < device_count; i++) {
			libusb_unref_device(device_list[i]);
		}
		free(device_list);
	}
	// TODO: move this state into a usb_sock_t
	if (context != NULL) {
		libusb_exit(context);
	}
	return NULL;
}
