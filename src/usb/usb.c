#include <stdio.h>
#include <stdlib.h>

#include <libusb.h>

#include "../http/http.h"
#include "usb.h"

#define USB_CONTEXT NULL

int is_ippusb_interface(const struct libusb_interface_descriptor *interf)
{
	return interf->bInterfaceClass == 0x07 &&
	       interf->bInterfaceSubClass == 0x01 &&
	       interf->bInterfaceProtocol == 0x04;
}

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
			if (!is_ippusb_interface(alt))
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


	// Discover device and count interfaces ==---------------------------==
	int selected_config = -1;
	int selected_ipp_interface_count = 0;

	libusb_device *printer_device = NULL;
	for (ssize_t i = 0; i < device_count; i++) {
		libusb_device *candidate = device_list[i];
		struct libusb_device_descriptor desc;
		libusb_get_device_descriptor(candidate, &desc);

		if (desc.idVendor != 0x03f0 &&
		    desc.idProduct != 0xc511)
			continue;
		// TODO: filter on serial number

		// TODO: search only device current config
		for (uint8_t config_num = 0;
		     config_num < desc.bNumConfigurations;
		     config_num++) {
			struct libusb_config_descriptor *config = NULL;
			status = libusb_get_config_descriptor(candidate,
			                                      config_num,
			                                      &config);
			// TODO: check status

			int interface_count = count_ippoverusb_interfaces(config);
			libusb_free_config_descriptor(config);
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


	// Open the printer ==-----------------------------------------------==
	status = libusb_open(printer_device, &usb->printer);
	if (status != 0) {
		ERR("failed to open device");
		goto error;
	}
	// Ask kernel to let go of any interfaces we need if it claimed them
	libusb_set_auto_detach_kernel_driver(usb->printer, 1);


	// Open every IPP-USB interface ==-----------------------------------==
	usb->num_interfaces = selected_ipp_interface_count;
	usb->interface_indexes = calloc(selected_ipp_interface_count,
	                                sizeof *(usb->interface_indexes));

	struct libusb_config_descriptor *config = NULL;
	status = libusb_get_config_descriptor(printer_device,
	                                      selected_config,
	                                      &config);
	// TODO: check status
	int interfs = selected_ipp_interface_count;
	for (uint8_t interf_num = 0;
	     interf_num < config->bNumInterfaces;
	     interf_num++) {
	
		const struct libusb_interface *interf = NULL;
		interf = &config->interface[interf_num];
		for (int alt_num = 0;
		     alt_num < interf->num_altsetting;
		     alt_num++) {
	
			const struct libusb_interface_descriptor *alt = NULL;
			alt = &interf->altsetting[alt_num];
			if (!is_ippusb_interface(alt))
				continue;

			// Claim the whole interface
			status = libusb_claim_interface(
					usb->printer,
					alt->bInterfaceNumber);
			switch (status) {
			case LIBUSB_ERROR_NOT_FOUND:
				ERR("USB Interface did not exist");
				goto error_config;
				break;
			case LIBUSB_ERROR_BUSY:
				ERR("Printer was already claimed");
				goto error_config;
				break;
			case LIBUSB_ERROR_NO_DEVICE:
				ERR("Printer was already claimed");
				goto error_config;
				break;
			case 0:
				break;
			}

			// Select the IPP-USB alt setting of the interface
			libusb_set_interface_alt_setting(usb->printer,
			                                 interf_num,
			                                 alt_num);
			interfs--;
			usb->interface_indexes[interfs] = interf_num;
			break;
		}
	}
	libusb_free_config_descriptor(config);


	libusb_free_device_list(device_list, 1);
	return usb;
error_config:
	if (config != NULL)
		libusb_free_config_descriptor(config);
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
	for (uint32_t i = 0; i < usb->num_interfaces; i++) {
		int index = usb->interface_indexes[i];
		libusb_release_interface(usb->printer, index);
	}
	libusb_close(usb->printer);
	libusb_exit(usb->context);
	free(usb);
	return;
}
