#define  _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <string.h>

#include <libusb.h>

#include "options.h"
#include "logging.h"
#include "http.h"
#include "usb.h"

#define IGNORE(x) (void)(x)

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

static int is_our_device(libusb_device *dev,
                         struct libusb_device_descriptor desc)
{
	static const int SERIAL_MAX = 1024;
	unsigned char serial[1024];
	if ((g_options.vendor_id  && desc.idVendor  != g_options.vendor_id)  &&
	    (g_options.product_id && desc.idProduct != g_options.product_id))
		return 0;

	if (g_options.serial_num == NULL)
		return 1;

	libusb_device_handle *handle = NULL;
	int status = libusb_open(dev, &handle);
	if (status != 0)
		return 0;

	status = libusb_get_string_descriptor_ascii(handle,
			desc.iSerialNumber, serial, SERIAL_MAX);
	libusb_close(handle);

	if (status <= 0) {
		WARN("Failed to get serial from device");
		return 0;
	}

	return strcmp((char *)serial, (char *)g_options.serial_num) == 0;
}

struct usb_sock_t *usb_open()
{
	struct usb_sock_t *usb = calloc(1, sizeof *usb);
	int status = 1;
	status = libusb_init(&usb->context);
	if (status < 0) {
		ERR("libusb init failed with error: %s",
			libusb_error_name(status));
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
	unsigned int selected_ipp_interface_count = 0;
	int auto_pick = !(g_options.vendor_id ||
	                  g_options.product_id ||
	                  g_options.serial_num);

	libusb_device *printer_device = NULL;
	for (ssize_t i = 0; i < device_count; i++) {
		libusb_device *candidate = device_list[i];
		struct libusb_device_descriptor desc;
		libusb_get_device_descriptor(candidate, &desc);

		if (!is_our_device(candidate, desc))
			continue;

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
				selected_ipp_interface_count = (unsigned) interface_count;
				printer_device = candidate;
				goto found_device;
			}

			// CONFTEST: Two or more interfaces are required
			if (interface_count == 1) {
				CONF("usb device has only one ipp interface "
				    "in violation of standard");
				goto error;
			}

			if (!auto_pick) {
				ERR_AND_EXIT("No ipp-usb interfaces found");
			}
		}
	}
found_device:

	if (printer_device == NULL) {
		if (!auto_pick) {
			ERR("No printer found by that vid, pid, serial");
		} else {
			ERR("No IPP over USB printer found");
		}
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

	unsigned int interfs = selected_ipp_interface_count;
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

			interfs--;

			struct usb_interface *uf = usb->interfaces + interfs;
			uf->interface_number = interf_num;
			uf->libusb_interface_index = alt->bInterfaceNumber;
			uf->interface_alt = alt_num;

			// Store interface's two endpoints
			for (int end_i = 0; end_i < alt->bNumEndpoints;
			     end_i++) {
				const struct libusb_endpoint_descriptor *end;
				end = &alt->endpoint[end_i];

				usb->max_packet_size = end->wMaxPacketSize;

				// High bit set means endpoint
				// is an INPUT or IN endpoint.
				uint8_t address = end->bEndpointAddress;
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

	// Pool management lock
	status_lock = sem_init(&usb->pool_manage_lock, 0, 1);
	if (status_lock != 0) {
		ERR("Failed to create high priority pool lock");
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

int usb_can_callback(struct usb_sock_t *usb)
{
	IGNORE(usb);

	if (!g_options.vendor_id ||
	    !g_options.product_id)
	{
		NOTE("Exit-on-unplug requires vid & pid");
		return 0;
	}

	int works = !!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG);
	if (!works)
		WARN("Libusb cannot tell us when to disconnect");
	return works;
}

static int LIBUSB_CALL usb_exit_on_unplug(libusb_context *context,
					  libusb_device *device,
					  libusb_hotplug_event event,
					  void *call_data)
{
	IGNORE(context);
	IGNORE(event);
	IGNORE(call_data);

	NOTE("Received unplug callback");

	struct libusb_device_descriptor desc;
	libusb_get_device_descriptor(device, &desc);

	if (is_our_device(device, desc))
		exit(0);

	return 0;
}

static void *usb_pump_events(void *user_data)
{
	IGNORE(user_data);

	for (;;) {
		// NOTE: This is a blocking call so
		// no need for sleep()
		libusb_handle_events_completed(NULL, NULL);
	}
}

void usb_register_callback(struct usb_sock_t *usb)
{
	IGNORE(usb);

	int status = libusb_hotplug_register_callback(
			NULL,
			LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
			// Note: libusb's enum has no default value
			// a bug has been filled with libusb.
			// Please switch the below line to 0
			// once the issue has been fixed in
			// deployed versions of libusb
			// https://github.com/libusb/libusb/issues/35
			// 0,
			LIBUSB_HOTPLUG_ENUMERATE,
			g_options.vendor_id,
			g_options.product_id,
			LIBUSB_HOTPLUG_MATCH_ANY,
			&usb_exit_on_unplug,
			NULL,
			NULL);
	if (status == LIBUSB_SUCCESS) {
		pthread_t thread_handle;
		pthread_create(&thread_handle, NULL, &usb_pump_events, NULL);
		NOTE("Registered unplug callback");
	} else
		ERR("Failed to register unplug callback");
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
		sem_wait(&usb->pool_manage_lock);
		{
			staled = usb->num_staled == usb->num_taken;
		}
		sem_post(&usb->pool_manage_lock);
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

	sem_wait(&usb->pool_manage_lock);
	{
		conn->parent = usb;
		conn->is_high_priority = used_high_priority;

		usb->num_taken++;
		uint32_t slot = --usb->num_avail;

		conn->interface_index = usb->interface_pool[slot];
		conn->interface = usb->interfaces + conn->interface_index;
		struct usb_interface *uf = conn->interface;

		// Make kernel release interface
		if (libusb_kernel_driver_active(usb->printer,
		                            uf->libusb_interface_index) == 1) {
			// Only linux supports this
			// other platforms will fail
			// thus we ignore the error code
			// it either works or it does not
			libusb_detach_kernel_driver(usb->printer,
			                           uf->libusb_interface_index);
		}

		// Claim the whole interface
		int status = 0;
		do {
			// Spinlock-like
			// Libusb does not offer a blocking call
			// so we're left with a spinlock
			status = libusb_claim_interface(
				usb->printer, uf->libusb_interface_index);
			switch (status) {
			case LIBUSB_ERROR_NOT_FOUND:
				ERR_AND_EXIT("USB Interface did not exist");
			case LIBUSB_ERROR_NO_DEVICE:
				ERR_AND_EXIT("Printer was removed");
			default:
				break;
			}
		} while (status != 0);

		// Select the IPP-USB alt setting of the interface
		libusb_set_interface_alt_setting(usb->printer,
		                                 uf->libusb_interface_index,
		                                 uf->interface_alt);
	}
	sem_post(&usb->pool_manage_lock);
	return conn;
}

void usb_conn_release(struct usb_conn_t *conn)
{
	struct usb_sock_t *usb = conn->parent;
	sem_wait(&usb->pool_manage_lock);
	{
		libusb_release_interface(usb->printer,
				conn->interface->libusb_interface_index);

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
	sem_post(&usb->pool_manage_lock);
}

void usb_conn_packet_send(struct usb_conn_t *conn, struct http_packet_t *pkt)
{
	int size_sent = 0;
	const int timeout = 6 * 60 * 60 * 1000; // 6 hours in milliseconds
	int num_timeouts = 0;
	size_t sent = 0;
	size_t pending = pkt->filled_size;
	while (pending > 0) {
		int to_send = (int)pending;

		NOTE("USB: want to send %d bytes", to_send);
		int status = libusb_bulk_transfer(conn->parent->printer,
		                                  conn->interface->endpoint_out,
		                                  pkt->buffer + sent, to_send,
		                                  &size_sent, timeout);
		if (status == LIBUSB_ERROR_TIMEOUT) {
			NOTE("USB: send timed out, retrying");

			if (num_timeouts++ > PRINTER_CRASH_TIMEOUT)
				ERR_AND_EXIT("Usb send fully timed out");

			// Sleep for tenth of a second
			struct timespec sleep_dur;
			sleep_dur.tv_sec = 0;
			sleep_dur.tv_nsec = 100000000;
			nanosleep(&sleep_dur, NULL);
			continue;
		}

		if (status == LIBUSB_ERROR_NO_DEVICE)
			ERR_AND_EXIT("Printer has been disconnected");
		if (status < 0)
			ERR_AND_EXIT("USB: send failed with status %s",
				libusb_error_name(status));
		if (size_sent < 0)
			ERR_AND_EXIT("Unexpected negative size_sent");

		pending -= (size_t) size_sent;
		sent += (size_t) size_sent;
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
	const int timeout = 6 * 60 * 60 * 1000; // 6 hours in milliseconds
	size_t read_size_ulong = packet_pending_bytes(pkt);
	if (read_size_ulong == 0)
		return pkt;

	int times_staled = 0;
	while (read_size_ulong > 0 && !msg->is_completed) {
		if (read_size_ulong >= INT_MAX)
			goto cleanup;
		int read_size = (int)read_size_ulong;

		// Ensure read_size is multiple of usb packets
		read_size += (512 - (read_size % 512)) % 512;

		// Expand buffer if needed
		if (pkt->buffer_capacity < pkt->filled_size + read_size_ulong)
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

		if (gotten_size < 0)
			ERR_AND_EXIT("Negative read size unexpected");

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

				if (pkt->filled_size > 0)
					NOTE("Packet so far \n%.*s\n",
						pkt->filled_size,
						pkt->buffer);
			}

			// Sleep for tenth of a second
			struct timespec sleep_dur;
			sleep_dur.tv_sec = 0;
			sleep_dur.tv_nsec = 100000000;
			nanosleep(&sleep_dur, NULL);
		}

		NOTE("USB: Got %d bytes", gotten_size);
		packet_mark_received(pkt, (size_t)gotten_size);
		read_size_ulong = packet_pending_bytes(pkt);
	}
	NOTE("USB: Received %d bytes of %d with type %d",
			pkt->filled_size, pkt->expected_size, msg->type);

	return pkt;

cleanup:
	if (pkt != NULL)
		packet_free(pkt);
	return NULL;
}
