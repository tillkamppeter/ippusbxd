#include <stdio.h>
#include <stdlib.h>

#include <libusb.h>

#include "logging.h"
#include "http.h"
#include "usb.h"

#define USB_CONTEXT NULL

static int is_ippusb_interface(const struct libusb_interface_descriptor *interf)
{
	return interf->bInterfaceClass == 0x07 &&
	       interf->bInterfaceSubClass == 0x01 &&
	       interf->bInterfaceProtocol == 0x04;
}

static int count_ippoverusb_interfaces(struct libusb_config_descriptor *config)
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

// TODO: refactor this
struct usb_sock_t *open_usb()
{
	struct usb_sock_t *usb = calloc(1, sizeof *usb);
	int status = 1;
	status = libusb_init(&usb->context);
	if (status < 0) {
		// TODO: use libusb_error_name for better status errors
		ERR("libusb init failed with error code %d", status);
		goto error_usbinit;
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

		// TODO: use libusb_cpu_to_le16 to fix endianess
		if (desc.idVendor  != 0x03f0 &&
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

			// CONFTEST: Two or more interfaces are required
			if (interface_count == 1) {
				CONF("usb device has only one ipp interface "
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


	// Open every IPP-USB interface ==-----------------------------------==
	usb->num_interfaces = selected_ipp_interface_count;
	usb->interfaces = calloc(usb->num_interfaces,
	                         sizeof(*usb->interfaces));
	if (usb->interfaces == NULL) {
		ERR("Failed to alloc space for interfaces");
		goto error_interfaces;
	}

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

			// Skip non-IPP-USB interfaces
			if (!is_ippusb_interface(alt))
				continue;

			// Release kernel driver
			if (libusb_kernel_driver_active(usb->printer,
			                                interf_num) == 1) {
				// Only linux supports this
				// other platforms will fail
				// thus we ignore the error code
				// it either works or it does not
				libusb_detach_kernel_driver(usb->printer,
				                            interf_num);
			}

			// Claim the whole interface
			status = libusb_claim_interface(
					usb->printer,
					alt->bInterfaceNumber);
			switch (status) {
			case LIBUSB_ERROR_NOT_FOUND:
				ERR("USB Interface did not exist");
				goto error_config;
			case LIBUSB_ERROR_BUSY:
				ERR("Printer was already claimed");
				goto error_config;
			case LIBUSB_ERROR_NO_DEVICE:
				ERR("Printer was already claimed");
				goto error_config;
			case 0:
				break;
			}

			// Select the IPP-USB alt setting of the interface
			libusb_set_interface_alt_setting(usb->printer,
			                                 interf_num,
			                                 alt_num);
			interfs--;
			usb->interfaces[interfs].interface_number = interf_num;

			// TODO: add conftest for endpoint count and direction
			// Store interface's two endpoints
			for (int end_i = 0; end_i < alt->bNumEndpoints;
			     end_i++) {
				const struct libusb_endpoint_descriptor *end = NULL;
				end = &alt->endpoint[end_i];

				// TODO: handle endianness
				usb->max_packet_size = end->wMaxPacketSize;

				// High bit set means endpoint
				// is an INPUT or IN endpoint.
				uint8_t address = end->bEndpointAddress;
				struct usb_interface *uf =
						usb->interfaces + interfs;
				if (address & 0x80)
					uf->endpoint_in = address;
				else
					uf->endpoint_out = address;
			}

			break;
		}
	}
	libusb_free_config_descriptor(config);


	libusb_free_device_list(device_list, 1);
	return usb;
error_config:
	if (config != NULL)
		libusb_free_config_descriptor(config);
error_interfaces:
	if (usb->interfaces != NULL)
		free(usb->interfaces);
error:
	if (device_list != NULL)
		libusb_free_device_list(device_list, 1);
	if (usb != NULL) {
		if (usb->context != NULL) {
			libusb_exit(usb->context);
		}
		free(usb);
	}
error_usbinit:
	return NULL;
}

void usb_close(struct usb_sock_t *usb)
{
	for (uint32_t i = 0; i < usb->num_interfaces; i++) {
		int number = usb->interfaces[i].interface_number;
		libusb_release_interface(usb->printer, number);
	}
	libusb_close(usb->printer);
	libusb_exit(usb->context);
	free(usb);
	return;
}

void send_packet_usb(struct usb_sock_t *usb, struct http_packet_t *pkt)
{
	// TODO: lock priority interfaces
	// TODO: transfer in max length chunks
	int size_sent = 0;
	int timeout = 1000; // in milliseconds
	int status = libusb_bulk_transfer(usb->printer,
	                                  usb->interfaces[0].endpoint_out,
	                                  pkt->buffer, pkt->filled_size,
	                                  &size_sent, timeout);
	printf("Note: sent %d bytes with status %d\n", size_sent, status);
}

struct http_packet_t *get_packet_usb(struct usb_sock_t *usb, struct http_message_t *msg)
{
	// TODO: Make usb use a message, but first messages need to do things
	struct http_packet_t *pkt = packet_new(msg);
	if (pkt == NULL) {
		ERR("failed to create packet struct for usb connection");
		goto error;
	}

	while (!(packet_at_capacity(pkt) || msg->is_completed)) {

		// Clasify message and desired packet size
		int bytes_to_read = 0;
		int pending_bytes = packet_pending_bytes(pkt);
		if (pending_bytes == 0) {
			break;
		} else if (pending_bytes < 0) {
			bytes_to_read = pkt->buffer_capacity - pkt->filled_size;

			if (packet_at_capacity(pkt))
				break;
		} else {
			bytes_to_read = pending_bytes;
		}


		// File packet
		int size_sent = 0;
		int status = 0;
		while (size_sent == 0) {
			// TODO: yield instead of spinlock
			const int timeout = 1000; // in milliseconds
			status = libusb_bulk_transfer(
			                      usb->printer,
			                      usb->interfaces[0].endpoint_in,
			                      pkt->buffer + pkt->filled_size,
			                      bytes_to_read,
			                      &size_sent, timeout);
			//printf("to read: %d\n", bytes_to_read);
			if (status != 0)
				ERR("bulk xfer failed with error code %d", status);
		}

		packet_mark_received(pkt, size_sent);

		printf("==-- %d Msg = (%lu of %lu), pkt = (%lu of %lu)\n",
		       msg->type, msg->received_size, msg->claimed_size,
		       pkt->filled_size, pkt->expected_size);
		printf("Data (%d bytes)\n%*s\n", size_sent, size_sent,
		       pkt->buffer + pkt->filled_size - size_sent);

	}

	printf("==-- End of packet --=\n");

	return pkt;

error:
	if (pkt != NULL)
		packet_free(pkt);
	return NULL;
}
