#pragma once

#include <libusb.h>

struct usb_interface {
	uint8_t interface_number;
	uint8_t endpoint_in;
	uint8_t endpoint_out;
};

struct usb_sock_t {
	libusb_context *context;
	libusb_device_handle *printer;
	int max_packet_size;

	uint32_t num_interfaces;
	struct usb_interface *interfaces;
};

struct usb_sock_t *open_usb(void);
void close_usb(struct usb_sock_t *);

void send_packet_usb(struct usb_sock_t *, struct http_packet_t *);

struct http_packet_t *get_packet_usb(struct usb_sock_t *);
