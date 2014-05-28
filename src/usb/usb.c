#include <stdio.h>

#include <libusb.h>

#include "../http/http.h"

typedef struct {
	int blah;
} usb_sock;

usb_sock *open_usb()
{
	int status = 1;
	status = libusb_init(NULL);
	if (status < 0) {
		ERR("libusb init failed with error code %d", status);
		return NULL;
	}
	return NULL;
}
