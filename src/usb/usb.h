#pragma once

#include <libusb.h>

typedef struct {
	libusb_context *context;
	libusb_device_handle *printer;
	int max_packet_size;

	uint32_t num_interfaces;
	int *interface_indexes;
	uint8_t *endpoints_in;
	uint8_t *endpoints_out;
} usb_sock_t;

usb_sock_t *open_usb();
void close_usb(usb_sock_t *);
