#pragma once

#include <libusb.h>
#include <semaphore.h>

static const int PRINTER_CRASH_TIMEOUT = 60;
static const int CONN_STALE_THRESHHOLD = 6;

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

	uint32_t num_staled;
	sem_t num_staled_lock;

	// TODO: add lock for pool
	uint32_t num_avail;
	uint32_t num_taken;

	uint32_t *interface_pool;
	sem_t pool_low_priority_lock;
	sem_t pool_high_priority_lock;

};

struct usb_conn_t {
	struct usb_sock_t *parent;
	struct usb_interface *interface;
	uint32_t interface_index;
	int is_high_priority;
	int is_staled;
};

struct usb_sock_t *usb_open(void);
void usb_close(struct usb_sock_t *);

struct usb_conn_t *usb_conn_aquire(struct usb_sock_t *, int);
void usb_conn_release(struct usb_conn_t *);

void usb_conn_packet_send(struct usb_conn_t *, struct http_packet_t *);
struct http_packet_t *usb_conn_packet_get(struct usb_conn_t *, struct http_message_t *);

