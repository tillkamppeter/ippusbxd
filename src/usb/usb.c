#include <stdio.h>
#include <stdlib.h>

#include <libusb.h>

#include "../http/http.h"
#include "usb.h"

#define USB_CONTEXT NULL

int count_ippoverusb_interfaces(struct libusb_config_descriptor *config)
{
	int ippusb_interface_count = 0;

	for (uint8_t interface_num = 0;
	     interface_num < config->bNumInterfaces;
	     interface_num++) {
	
		const struct libusb_interface *interface = NULL;
		interface = &config->interface[interface_num];
	
		for (int alt_num = 0;
		     alt_num < interface->num_altsetting;
		     alt_num++) {
	
			const struct libusb_interface_descriptor *alt = NULL;
			alt = &interface->altsetting[alt_num];
	
			// Check for IPP over USB interfaces
			if (alt->bInterfaceClass != 0x07 ||
			    alt->bInterfaceSubClass != 0x01 ||
			    alt->bInterfaceProtocol != 0x04)
				continue;
	
			ippusb_interface_count++;
			break;
		}
	}

	return ippusb_interface_count;
}

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


	int selected_config = -1;
	int selected_ipp_interface_count = 0;
	libusb_device *printer_device = NULL;
	for (ssize_t i = 0; i < device_count; i++) {
		libusb_device *candidate = device_list[i];
		struct libusb_device_descriptor desc;
		libusb_get_device_descriptor(candidate, &desc);
		// TODO: error if device is not printer supporting IPP
		if (desc.idVendor != 0x03f0 &&
		    desc.idProduct != 0xc511)
			continue;
		// TODO: filter on serial number

		for (uint8_t config_num = 0;
		     config_num < desc.bNumConfigurations;
		     config_num++) {
			struct libusb_config_descriptor *config = NULL;
			status = libusb_get_config_descriptor(candidate,
			                                      config_num,
			                                      &config);

			int interface_count = count_ippoverusb_interfaces(config);
			if (interface_count >= 2) {
				selected_config = config_num;
				selected_ipp_interface_count = interface_count;
				printer_device = candidate;
				goto found_target_device;
			}
			if (interface_count == 1) {
				ERR("usb device has only one ipp interface "
				    "in violation of standard");
				goto error;
			}
			ERR("usb device had no ipp-usb class interfaces");
			// TODO: if VID and PID set warn 
			// that the device is not a ipp printer
		}
	}
found_target_device:

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


	libusb_free_device_list(device_list, 1);
	return usb;

error:
	if (device_list != NULL) {
		libusb_free_device_list(device_list, 1);
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
