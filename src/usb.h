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
	uint32_t num_alloced;
};

struct usb_conn_t {
	struct usb_sock_t *parent;
	struct usb_interface *interface;
};

struct usb_sock_t *usb_open(void);
void usb_close(struct usb_sock_t *);

struct usb_conn_t *usb_conn_get(struct usb_sock_t *);
void usb_conn_free(struct usb_conn_t *);

void usb_conn_packet_send(struct usb_conn_t *, struct http_packet_t *);
struct http_packet_t *usb_conn_packet_get(struct usb_conn_t *, struct http_message_t *);

