#pragma once

#include <libusb.h>
#include <semaphore.h>

struct usb_interface {
	uint8_t interface_number;
	uint8_t endpoint_in;
	uint8_t endpoint_out;
};

// TODO: add lock on socket
struct usb_sock_t {
	libusb_context *context;
	libusb_device_handle *printer;
	int max_packet_size;

	uint32_t num_interfaces;
	struct usb_interface *interfaces;

	uint32_t num_avail;
	uint32_t *interface_pool;
	sem_t pool_lock;
};

struct usb_conn_t {
	struct usb_sock_t *parent;
	struct usb_interface *interface;
	uint32_t interface_index;
};

struct usb_sock_t *usb_open(void);
void usb_close(struct usb_sock_t *);

struct usb_conn_t *usb_conn_aquire(struct usb_sock_t *);
void usb_conn_release(struct usb_conn_t *);

void usb_conn_packet_send(struct usb_conn_t *, struct http_packet_t *);
struct http_packet_t *usb_conn_packet_get(struct usb_conn_t *, struct http_message_t *);

