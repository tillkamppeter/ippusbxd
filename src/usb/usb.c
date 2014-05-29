#include <stdio.h>
#include <stdlib.h>

#include <libusb.h>

#include "../http/http.h"
#include "usb.h"

#define USB_CONTEXT NULL

usb_sock_t *open_usb()
{
	usb_sock_t *usb = calloc(1, sizeof *usb);
	int status = 1;
	status = libusb_init(&usb->context);
	if (status < 0) {
		ERR("libusb init failed with error code %d", status);
		goto error;
	}

	libusb_device **device_list = NULL;
	ssize_t device_count = libusb_get_device_list(usb->context, &device_list);
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

	status = libusb_open(printer_device, &usb->printer);
	if (status != 0) {
		ERR("failed to open device");
		goto error;
	}


	// TODO: close printer with libusb_close()


	return usb;

error:
	if (device_list != NULL) {
		for (ssize_t i = 0; i < device_count; i++) {
			libusb_unref_device(device_list[i]);
		}
		free(device_list);
	}
	// TODO: move this state into a usb_sock_t
	if (usb != NULL) {
		if (usb->context != NULL) {
			libusb_exit(usb->context);
		}
		free(usb);
	}
	return NULL;
}

void close_usb(usb_sock_t *usb)
{
	libusb_close(usb->printer);
	libusb_exit(usb->context);
	free(usb);
	return;
}
