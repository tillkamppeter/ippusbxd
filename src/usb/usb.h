#pragma once

#include <libusb.h>

typedef struct {
	libusb_context *context;
	libusb_device_handle *printer;
} usb_sock_t;

usb_sock_t *open_usb();
void close_usb(usb_sock_t *);
