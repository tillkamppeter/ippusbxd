#define  _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

#include <libusb.h>

#include "logging.h"
#include "http.h"
#include "usb.h"

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

struct usb_sock_t *usb_open()
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
			if (status < 0)
				ERR_AND_EXIT("USB: didn't get config desc %s",
					libusb_error_name(status));

			int interface_count = count_ippoverusb_interfaces(config);
			libusb_free_config_descriptor(config);
			if (interface_count >= 2) {
				selected_config = config_num;
				selected_ipp_interface_count = interface_count;
				printer_device = candidate;
				goto found_device;
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
found_device:

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
		ERR("Failed to alloc interfaces");
		goto error;
	}

	struct libusb_config_descriptor *config = NULL;
	status = libusb_get_config_descriptor(printer_device,
	                                      (uint8_t)selected_config,
	                                      &config);
	if (status != 0 || config == NULL) {
		ERR("Failed to aquire config descriptor");
		goto error;
	}

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


	// Pour interfaces into pool ==--------------------------------------==
	usb->num_avail = usb->num_interfaces;
	usb->interface_pool = calloc(usb->num_avail,
	                             sizeof(*usb->interface_pool));
	if (usb->interface_pool == NULL) {
		ERR("Failed to alloc interface pool");
		goto error;
	}
	for (uint32_t i = 0; i < usb->num_avail; i++) {
		usb->interface_pool[i] = i;
	}

	// Stale lock
	int status_lock = sem_init(&usb->num_staled_lock, 0, 1);
	if (status_lock != 0) {
		ERR("Failed to create num_staled lock");
		goto error;
	}

	// High priority lock
	status_lock = sem_init(&usb->pool_high_priority_lock, 0, 1);
	if (status_lock != 0) {
		ERR("Failed to create low priority pool lock");
		goto error;
	}
	// Low priority lock
	status_lock = sem_init(&usb->pool_low_priority_lock,
	                       0, usb->num_avail - 1);
	if (status_lock != 0) {
		ERR("Failed to create high priority pool lock");
		goto error;
	}

	return usb;

error_config:
	if (config != NULL)
		libusb_free_config_descriptor(config);
error:
	if (device_list != NULL)
		libusb_free_device_list(device_list, 1);
error_usbinit:
	if (usb != NULL) {
		if (usb->context != NULL)
			libusb_exit(usb->context);
		if (usb->interfaces != NULL)
			free(usb->interfaces);
		if (usb->interface_pool != NULL)
			free(usb->interface_pool);
		free(usb);
	}
	return NULL;
}

void usb_close(struct usb_sock_t *usb)
{
	// Release interfaces
	for (uint32_t i = 0; i < usb->num_interfaces; i++) {
		int number = usb->interfaces[i].interface_number;
		libusb_release_interface(usb->printer, number);
	}

	libusb_close(usb->printer);
	libusb_exit(usb->context);

	sem_destroy(&usb->pool_high_priority_lock);
	sem_destroy(&usb->pool_low_priority_lock);
	sem_destroy(&usb->num_staled_lock);
	free(usb->interfaces);
	free(usb->interface_pool);
	free(usb);
	return;
}


static void usb_conn_mark_staled(struct usb_conn_t *conn)
{
	if (conn->is_staled)
		return;

	struct usb_sock_t *usb = conn->parent;

	sem_wait(&usb->num_staled_lock);
	{
		usb->num_staled++;
	}
	sem_post(&usb->num_staled_lock);

	conn->is_staled = 1;
}

static void usb_conn_mark_moving(struct usb_conn_t *conn)
{
	if (!conn->is_staled)
		return;

	struct usb_sock_t *usb = conn->parent;

	sem_wait(&usb->num_staled_lock);
	{
		usb->num_staled--;
	}
	sem_post(&usb->num_staled_lock);

	conn->is_staled = 0;
}

static int usb_all_conns_staled(struct usb_sock_t *usb)
{
	int staled;

	sem_wait(&usb->num_staled_lock);
	{
		// TODO: use pool lock
		staled = usb->num_staled == usb->num_taken;
	}
	sem_post(&usb->num_staled_lock);

	return staled;
}

struct usb_conn_t *usb_conn_aquire(struct usb_sock_t *usb,
                                   int is_high_priority)
{
	int used_high_priority = 0;
	if (is_high_priority) {
		// We first take the high priority lock.
		sem_wait(&usb->pool_high_priority_lock);
		used_high_priority = 1;

		// We can then check if a low priority
		// interface is available.
		if (!sem_trywait(&usb->pool_low_priority_lock)) {
			// If a low priority is avail we take that one
			// then release the high priority interface.
			// Otherwise we use the high priority interface.
			used_high_priority = 0;
			sem_post(&usb->pool_high_priority_lock);
		}
	} else {
		sem_wait(&usb->pool_low_priority_lock);
	}

	struct usb_conn_t *conn = calloc(1, sizeof(*conn));
	if (conn == NULL) {
		ERR("failed to aloc space for usb connection");
		return NULL;
	}

	conn->parent = usb;
	conn->is_high_priority = used_high_priority;

	usb->num_taken++;
	uint32_t slot = --usb->num_avail;
	conn->interface_index = usb->interface_pool[slot];
	conn->interface = usb->interfaces + conn->interface_index;
	return conn;
}

void usb_conn_release(struct usb_conn_t *conn)
{
	struct usb_sock_t *usb = conn->parent;
	// Return usb interface to pool
	usb->num_taken--;
	uint32_t slot = usb->num_avail++;
	usb->interface_pool[slot] = conn->interface_index;

	// Release our interface lock
	if (conn->is_high_priority)
		sem_post(&usb->pool_high_priority_lock);
	else
		sem_post(&usb->pool_low_priority_lock);

	free(conn);
}

void usb_conn_packet_send(struct usb_conn_t *conn, struct http_packet_t *pkt)
{
	int size_sent = 0;
	int timeout = 1000; // in milliseconds
	size_t sent = 0;
	size_t pending = pkt->filled_size;
	while (pending > 0) {
		int to_send = (int)pending;
		if (pending < 512)
			to_send = (int)pending;

		NOTE("USB: want to send %d bytes", to_send);
		int status = libusb_bulk_transfer(conn->parent->printer,
		                                  conn->interface->endpoint_out,
		                                  pkt->buffer + sent, to_send,
		                                  &size_sent, timeout);
		if (status == LIBUSB_ERROR_TIMEOUT) {
			// TODO: add our own timeout
			NOTE("USB: send timed out, retrying");
			continue;
		}
		if (status == LIBUSB_ERROR_NO_DEVICE)
			ERR_AND_EXIT("Printer has been disconnected");
		if (status < 0)
			ERR_AND_EXIT("USB: send failed with status %s",
				libusb_error_name(status));

		pending -= size_sent;
		sent += size_sent;
		NOTE("USB: sent %d bytes", size_sent);
	}
	NOTE("USB: sent %d bytes in total", sent);
}

struct http_packet_t *usb_conn_packet_get(struct usb_conn_t *conn, struct http_message_t *msg)
{
	if (msg->is_completed)
		return NULL;

	struct http_packet_t *pkt = packet_new(msg);
	if (pkt == NULL) {
		ERR("failed to create packet for incoming usb message");
		goto cleanup;
	}

	// File packet
	const int timeout = 1000; // in milliseconds
	ssize_t read_size_raw = packet_pending_bytes(pkt);
	if (read_size_raw == 0)
		return pkt;
	else if (read_size_raw < 0)
		goto cleanup;


	int times_staled = 0;
	while (read_size_raw > 0 && !msg->is_completed) {
		if (read_size_raw >= INT_MAX)
			goto cleanup;
		int read_size = (int)read_size_raw;

		// Ensure read_size is multiple of usb packets
		read_size += (512 - (read_size % 512)) % 512;

		// Expand buffer if needed
		if (pkt->buffer_capacity < pkt->filled_size + read_size)
			if (packet_expand(pkt) < 0)
				ERR_AND_EXIT("Failed to ensure room for usb pkt");

		NOTE("USB: Getting %d bytes of %d", read_size, pkt->expected_size);
		int gotten_size = 0;
		int status = libusb_bulk_transfer(
		                      conn->parent->printer,
		                      conn->interface->endpoint_in,
		                      pkt->buffer + pkt->filled_size,
		                      read_size,
		                      &gotten_size, timeout);

		if (status == LIBUSB_ERROR_NO_DEVICE)
			ERR_AND_EXIT("Printer has been disconnected");

		if (status != 0 && status != LIBUSB_ERROR_TIMEOUT) {
			ERR("bulk xfer failed with error code %d", status);
			ERR("tried reading %d bytes", read_size);
			goto cleanup;
		}

		if (gotten_size > 0) {
			times_staled = 0;
			usb_conn_mark_moving(conn);
		} else {
			times_staled++;
			if (times_staled > CONN_STALE_THRESHHOLD) {
				usb_conn_mark_staled(conn);

				if (usb_all_conns_staled(conn->parent) &&
				    times_staled > PRINTER_CRASH_TIMEOUT) {
					ERR("USB timedout, dropping data");
					goto cleanup;
				}
			}

			// Sleep for tenth of a second
			struct timespec sleep_dur;
			sleep_dur.tv_sec = 0;
			sleep_dur.tv_nsec = 100000000;
			nanosleep(&sleep_dur, NULL);
		}

		NOTE("USB: Got %d bytes", gotten_size);
		packet_mark_received(pkt, gotten_size);
		read_size_raw = packet_pending_bytes(pkt);
	}
	NOTE("USB: Received %d bytes of %d with type %d",
			pkt->filled_size, pkt->expected_size, msg->type);

	return pkt;

cleanup:
	if (pkt != NULL)
		packet_free(pkt);
	return NULL;
}
