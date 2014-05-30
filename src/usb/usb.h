#pragma once

#include <libusb.h>

typedef struct {
	libusb_context *context;
	libusb_device_handle *printer;

	uint32_t num_interfaces;
	struct libusb_interface *interfaces;
} usb_sock_t;

usb_sock_t *open_usb();
void close_usb(usb_sock_t *);
