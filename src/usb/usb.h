#pragma once

#include <libusb.h>

struct usb_interface {
	uint8_t interface_number;
	uint8_t endpoint_in;
	uint8_t endpoint_out;
};

typedef struct {
	libusb_context *context;
	libusb_device_handle *printer;
	int max_packet_size;

	uint32_t num_interfaces;
	struct usb_interface *interfaces;
} usb_sock_t;

usb_sock_t *open_usb();
void close_usb(usb_sock_t *);

void send_packet_usb(usb_sock_t *, http_packet_t *);
