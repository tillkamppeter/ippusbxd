/* Copyright (C) 2014 Daniel Dressler and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#define  _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include <libusb.h>

#include "options.h"
#include "dnssd.h"
#include "logging.h"
#include "http.h"
#include "tcp.h"
#include "usb.h"

#define IGNORE(x) (void)(x)

static int bus, dev_addr;

static int is_ippusb_interface(const struct libusb_interface_descriptor *interf)
{
  return interf->bInterfaceClass == 0x07 &&
    interf->bInterfaceSubClass == 0x01 &&
    interf->bInterfaceProtocol == 0x04;
}

static int count_ippoverusb_interfaces(struct libusb_config_descriptor *config)
{
  int ippusb_interface_count = 0;

  NOTE("Counting IPP-over-USB interfaces ...");
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
      NOTE("Interface %d, Alt %d: Class %d, Subclass %d, Protocol %d",
	   interface_num, alt_num, alt->bInterfaceClass,
	   alt->bInterfaceSubClass, alt->bInterfaceProtocol);

      /* Check for IPP over USB interfaces */
      if (!is_ippusb_interface(alt))
	continue;

      NOTE("   -> is IPP-over-USB");
      ippusb_interface_count++;
      break;
    }
  }

  NOTE("   -> %d Interfaces", ippusb_interface_count);
  return ippusb_interface_count;
}

static int is_our_device(libusb_device *dev,
                         struct libusb_device_descriptor desc)
{
  static const int SERIAL_MAX = 1024;
  unsigned char serial[1024];
  NOTE("Found device: VID %04x, PID %04x on Bus %03d, Device %03d",
       desc.idVendor, desc.idProduct,
       libusb_get_bus_number(dev), libusb_get_device_address(dev));
  if ((g_options.vendor_id && desc.idVendor != g_options.vendor_id) ||
      (g_options.product_id && desc.idProduct != g_options.product_id) ||
      (g_options.bus &&
       libusb_get_bus_number(dev) != g_options.bus) ||
      (g_options.device &&
       libusb_get_device_address(dev) != g_options.device))
    return 0;

  if (g_options.serial_num == NULL)
    return 1;

  libusb_device_handle *handle = NULL;
  int status = libusb_open(dev, &handle);
  if (status != 0) {
    /* Device turned off or disconnected, we cannot retrieve its
       serial number any more, so we identify it via bus and device
       addresses */
    return (bus == libusb_get_bus_number(dev) &&
	    dev_addr == libusb_get_device_address(dev));
  } else {
    /* Device is turned on and connected, read out its serial number
       and use the serial number for identification */
    status = libusb_get_string_descriptor_ascii(handle,
						desc.iSerialNumber,
						serial, SERIAL_MAX);
    libusb_close(handle);

    if (status <= 0) {
      WARN("Failed to get serial from device");
      return 0;
    }

    return strcmp((char *)serial,
		  (char *)g_options.serial_num) == 0;
  }
}

int get_device_id(struct libusb_device_handle *handle,
		  int conf,
		  int iface,
		  int altset,
		  char *buffer,
		  size_t bufsize)
{
  size_t	length;

  if (libusb_control_transfer(handle,
			      LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_ENDPOINT_IN |
			      LIBUSB_RECIPIENT_INTERFACE,
			      0, conf, (iface << 8) | altset,
			      (unsigned char *)buffer, bufsize, 5000) < 0) {
    *buffer = '\0';
    return (-1);
  }

  /* Extract the length of the device ID string from the first two
     bytes.  The 1284 spec says the length is stored MSB first... */
  length = (int)((((unsigned)buffer[0] & 255) << 8) |
		 ((unsigned)buffer[1] & 255));

  /* Check to see if the length is larger than our buffer or less than 14 bytes
     (the minimum valid device ID is "MFG:x;MDL:y;" with 2 bytes for the
     length).
     If the length is out-of-range, assume that the vendor incorrectly
     implemented the 1284 spec and re-read the length as LSB first, ... */
  if (length > bufsize || length < 14)
    length = (int)((((unsigned)buffer[1] & 255) << 8) |
		   ((unsigned)buffer[0] & 255));

  if (length > bufsize)
    length = bufsize;

  if (length < 14) {
    /* Invalid device ID, clear it! */
    *buffer = '\0';
    return (-1);
  }

  length -= 2;
  memmove(buffer, buffer + 2, (size_t)length);
  buffer[length] = '\0';
  return (0);
}

struct usb_sock_t *usb_open()
{
  int status_lock;
  struct usb_sock_t *usb = calloc(1, sizeof *usb);
  int status = 1;
  usb->device_id = NULL;
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

  /* Discover device and count interfaces ==---------------------------== */
  int selected_config = -1;
  unsigned int selected_ipp_interface_count = 0;
  int auto_pick = !((g_options.vendor_id &&
		     g_options.product_id) ||
		    g_options.serial_num ||
		    (g_options.bus &&
		     g_options.device));

  libusb_device *printer_device = NULL;

  if (g_options.vendor_id || g_options.product_id)
    NOTE("Searching for device: VID %04x, PID %04x",
	 g_options.vendor_id, g_options.product_id);
  if (g_options.serial_num)
    NOTE("Searching for device with serial number %s",
	 g_options.serial_num);
  if (g_options.bus || g_options.device)
    NOTE("Searching for device: Bus %03d, Device %03d",
	 g_options.bus, g_options.device);
  if (auto_pick)
    NOTE("Searching for first IPP-over-USB-capable device available");

  for (ssize_t i = 0; i < device_count; i++) {
    libusb_device *candidate = device_list[i];
    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(candidate, &desc);

    if (!is_our_device(candidate, desc))
      continue;

    /* Save VID/PID for exit-on-unplug */
    if (g_options.vendor_id == 0)
      g_options.vendor_id = desc.idVendor;
    if (g_options.product_id == 0)
      g_options.product_id = desc.idProduct;

    bus = libusb_get_bus_number(candidate);
    dev_addr = libusb_get_device_address(candidate);
    NOTE("Printer connected on bus %03d device %03d",
	 bus, dev_addr);

    for (uint8_t config_num = 0;
	 config_num < desc.bNumConfigurations;
	 config_num++) {
      struct libusb_config_descriptor *config = NULL;
      status = libusb_get_config_descriptor(candidate,
					    config_num,
					    &config);
      if (status < 0) {
	ERR("USB: didn't get config desc %s",
	    libusb_error_name(status));
	goto error;
      }

      int interface_count = count_ippoverusb_interfaces(config);
      libusb_free_config_descriptor(config);
      if (interface_count >= 2) {
	selected_config = config_num;
	selected_ipp_interface_count = (unsigned) interface_count;
	printer_device = candidate;
	goto found_device;
      }

      /* CONFTEST: Two or more interfaces are required */
      if (interface_count == 1) {
	CONF("usb device has only one ipp interface "
	     "in violation of standard");
	goto error;
      }

      if (!auto_pick) {
	ERR("No ipp-usb interfaces found");
	goto error;
      }
    }
  }
 found_device:

  if (printer_device == NULL) {
    if (!auto_pick) {
      ERR("No printer found by that vid, pid, serial or bus, device");
    } else {
      ERR("No IPP over USB printer found");
    }
    goto error;
  }

  /* Open the printer ==-----------------------------------------------== */
  status = libusb_open(printer_device, &usb->printer);
  if (status != 0) {
    ERR("failed to open device");
    goto error;
  }

  /* Open every IPP-USB interface ==-----------------------------------== */
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
    ERR("Failed to acquire config descriptor");
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

      /* Get the IEE-1284 device ID */
      if (usb->device_id == NULL) {
	usb->device_id = calloc(2048, sizeof(char));
	if (usb->device_id == NULL) {
	  ERR("Failed to allocate memory for the device ID");
	  goto error;
	}
	if (get_device_id(usb->printer, selected_config,
			  interf_num, alt_num,
			  usb->device_id, 2048) != 0 ||
	    strlen(usb->device_id) == 0) {
	  NOTE("Could not retrieve device ID for config #%d, interface #%d, alt setting #%d, will try with other combo ...",
	       selected_config, interf_num, alt_num);
	  free(usb->device_id);
	  usb->device_id = NULL;
	  g_options.device_id = NULL;
	} else {
	  NOTE("USB device ID: %s", usb->device_id);
	  g_options.device_id = usb->device_id;
	}
      }

      /* Skip non-IPP-USB interfaces */
      if (!is_ippusb_interface(alt))
	continue;

      interfs--;

      struct usb_interface *uf = usb->interfaces + interfs;
      uf->interface_number = interf_num;
      uf->libusb_interface_index = alt->bInterfaceNumber;
      uf->interface_alt = alt_num;

      /* Store interface's two endpoints */
      for (int end_i = 0; end_i < alt->bNumEndpoints;
	   end_i++) {
	const struct libusb_endpoint_descriptor *end;
	end = &alt->endpoint[end_i];

	usb->max_packet_size = end->wMaxPacketSize;

	/* High bit set means endpoint
	   is an INPUT or IN endpoint. */
	uint8_t address = end->bEndpointAddress;
	if (address & 0x80)
	  uf->endpoint_in = address;
	else
	  uf->endpoint_out = address;
      }

      status_lock = sem_init(&uf->lock, 0, 1);
      if (status_lock != 0) {
	ERR("Failed to create interface lock #%d",
	    interf_num);
	goto error;
      }

      break;
    }
  }
  libusb_free_config_descriptor(config);
  libusb_free_device_list(device_list, 1);

  /* Pour interfaces into pool ==--------------------------------------== */
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
  NOTE("USB interfaces pool: %d interfaces", usb->num_avail);

  /* Stale lock */
  status_lock = sem_init(&usb->num_staled_lock, 0, 1);
  if (status_lock != 0) {
    ERR("Failed to create num_staled lock");
    goto error;
  }

  /* Pool management lock */
  status_lock = sem_init(&usb->pool_manage_lock, 0, 1);
  if (status_lock != 0) {
    ERR("Failed to create pool management lock");
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
  /* Release interfaces */
  for (uint32_t i = 0; i < usb->num_interfaces; i++) {
    int number = usb->interfaces[i].interface_number;
    libusb_release_interface(usb->printer, number);
    sem_destroy(&usb->interfaces[i].lock);
  }

  NOTE("Resetting printer ...");
  libusb_reset_device(usb->printer);
  NOTE("Reset completed.");
  libusb_close(usb->printer);
  if (usb != NULL) {
    if (usb->context != NULL)
      libusb_exit(usb->context);
    sem_destroy(&usb->num_staled_lock);
    if (usb->interfaces != NULL)
      free(usb->interfaces);
    if (usb->interface_pool != NULL)
      free(usb->interface_pool);
    free(usb);
    usb = NULL;
  }
  return;
}

int usb_can_callback(struct usb_sock_t *usb)
{
  IGNORE(usb);

  if (!g_options.vendor_id ||
      !g_options.product_id) {
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

  if (is_our_device(device, desc)) {
    /* We prefer an immediate shutdown with only DNS-SD and TCP
       clean-up here as by a regular sgutdown request via termination
       flag g_options.terminate there can still happen USB
       communication attempts with long timeouts, making ippusbxd get
       stuck for a significant time.  This way we immediately stop the
       DNS-SD advertising and release the host/port binding. */

    /* Unregister DNS-SD for printer on Avahi */
    if (g_options.dnssd_data != NULL)
      dnssd_shutdown();

    /* TCP clean-up */
    if (g_options.tcp_socket!= NULL)
      tcp_close(g_options.tcp_socket);
    if (g_options.tcp6_socket!= NULL)
      tcp_close(g_options.tcp6_socket);

    exit(0);
  }

  return 0;
}

static void *usb_pump_events(void *user_data)
{
  IGNORE(user_data);

  NOTE("USB unplug event observer thread starting");

  while (!g_options.terminate) {
    /* NOTE: This is a blocking call so
       no need for sleep() */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    libusb_handle_events_timeout_completed(NULL, &tv, NULL);
  }

  NOTE("USB unplug event observer thread terminating");

  return NULL;
}

void usb_register_callback(struct usb_sock_t *usb)
{
  IGNORE(usb);

  int status =
    libusb_hotplug_register_callback(NULL,
				     LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
				     /* Note: libusb's enum has no default value
					a bug has been filled with libusb.
					Please switch the below line to 0
					once the issue has been fixed in
					deployed versions of libusb
					https://github.com/libusb/libusb/issues/35 */
				     /* 0, */
				     LIBUSB_HOTPLUG_ENUMERATE,
				     g_options.vendor_id,
				     g_options.product_id,
				     LIBUSB_HOTPLUG_MATCH_ANY,
				     &usb_exit_on_unplug,
				     NULL,
				     NULL);
  if (status == LIBUSB_SUCCESS) {
    pthread_create(&(g_options.usb_event_thread_handle), NULL, &usb_pump_events, NULL);
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

struct usb_conn_t *usb_conn_acquire(struct usb_sock_t *usb)
{
  int i;

  if (usb->num_avail <= 0) {
    NOTE("All USB interfaces busy, waiting ...");
    for (i = 0; i < 30 && usb->num_avail <= 0; i ++) {
      if (g_options.terminate)
	return NULL;
      usleep(100000);
    }
    if (usb->num_avail <= 0) {
      ERR("Timed out waiting for a free USB interface");
      return NULL;
    }
    usleep(100000);
  }

  struct usb_conn_t *conn = calloc(1, sizeof(*conn));
  if (conn == NULL) {
    ERR("Failed to alloc space for usb connection");
    return NULL;
  }

  sem_wait(&usb->pool_manage_lock);
  {
    conn->parent = usb;

    uint32_t slot = usb->num_taken;

    conn->interface_index = usb->interface_pool[slot];
    conn->interface = usb->interfaces + conn->interface_index;
    struct usb_interface *uf = conn->interface;

    /* Sanity check: Is the interface still free */
    if (sem_trywait(&uf->lock)) {
      ERR("Interface #%d (%d) already in use!",
	  conn->interface_index,
	  uf->libusb_interface_index);
      goto acquire_error;
    }

    /* Make kernel release interface */
    if (libusb_kernel_driver_active(usb->printer,
				    uf->libusb_interface_index) == 1) {
      /* Only linux supports this
	 other platforms will fail
	 thus we ignore the error code
	 it either works or it does not */
      libusb_detach_kernel_driver(usb->printer,
				  uf->libusb_interface_index);
    }

    /* Claim the whole interface */
    int status = 0;
    do {
      /* Spinlock-like
	 Libusb does not offer a blocking call
	 so we're left with a spinlock */
      status = libusb_claim_interface(usb->printer, uf->libusb_interface_index);
      if (status) NOTE("Failed to claim interface %d, retrying", conn->interface_index);
      switch (status) {
      case LIBUSB_ERROR_NOT_FOUND:
	ERR("USB Interface did not exist");
	goto acquire_error;
      case LIBUSB_ERROR_NO_DEVICE:
	ERR("Printer was removed");
	goto acquire_error;
      default:
	break;
      }
    } while (status != 0 && !g_options.terminate);

    if (g_options.terminate)
      goto acquire_error;

    /* Select the IPP-USB alt setting of the interface */
    libusb_set_interface_alt_setting(usb->printer,
				     uf->libusb_interface_index,
				     uf->interface_alt);

    /* Take successfully acquired interface from the pool */
    usb->num_taken++;
    usb->num_avail--;
  }
  sem_post(&usb->pool_manage_lock);
  return conn;

 acquire_error:

  sem_post(&usb->pool_manage_lock);
  free(conn);
  return NULL;
}

void usb_conn_release(struct usb_conn_t *conn)
{
  struct usb_sock_t *usb = conn->parent;
  sem_wait(&usb->pool_manage_lock);
  {
    int status = 0;
    do {
      /* Spinlock-like
	 libusb does not offer a blocking call
	 so we're left with a spinlock */
      status = libusb_release_interface(usb->printer,
					conn->interface->libusb_interface_index);
      if (status) NOTE("Failed to release interface %d, retrying", conn->interface_index);
    } while (status != 0 && !g_options.terminate);

    /* Return usb interface to pool */
    usb->num_taken--;
    usb->num_avail++;
    uint32_t slot = usb->num_taken;
    usb->interface_pool[slot] = conn->interface_index;

    /* Release our interface lock */
    sem_post(&conn->interface->lock);

    free(conn);
  }
  sem_post(&usb->pool_manage_lock);
}

int usb_conn_packet_send(struct usb_conn_t *conn, struct http_packet_t *pkt)
{
  int size_sent = 0;
  const int timeout = 1000; /* 1 sec */
  int num_timeouts = 0;
  size_t sent = 0;
  size_t pending = pkt->filled_size;
  while (pending > 0 && !g_options.terminate) {
    int to_send = (int)pending;

    NOTE("P %p: USB: want to send %d bytes", pkt, to_send);
    int status = libusb_bulk_transfer(conn->parent->printer,
				      conn->interface->endpoint_out,
				      pkt->buffer + sent, to_send,
				      &size_sent, timeout);
    if (status == LIBUSB_ERROR_NO_DEVICE) {
      ERR("P %p: Printer has been disconnected",
	  pkt);
      return -1;
    }
    if (status == LIBUSB_ERROR_TIMEOUT) {
      NOTE("P %p: USB: send timed out, retrying", pkt);

      if (num_timeouts++ > PRINTER_CRASH_TIMEOUT_RECEIVE) {
	ERR("P %p: Usb send fully timed out",
	    pkt);
	return -1;
      }

      /* Sleep for tenth of a second */
      struct timespec sleep_dur;
      sleep_dur.tv_sec = 0;
      sleep_dur.tv_nsec = 100000000;
      nanosleep(&sleep_dur, NULL);
      if (size_sent == 0)
	continue;
    } else if (status < 0) {
      ERR("P %p: USB: send failed with status %s",
	  pkt, libusb_error_name(status));
      return -1;
    }
    if (size_sent < 0) {
      ERR("P %p: Unexpected negative size_sent",
	  pkt);
      return -1;
    }

    pending -= (size_t) size_sent;
    sent += (size_t) size_sent;
    NOTE("P %p: USB: sent %d bytes", pkt, size_sent);
  }
  NOTE("P %p: USB: sent %d bytes in total", pkt, sent);
  return 0;
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

  /* File packet */
  const int timeout = 1000; /* 1 sec */
  size_t read_size_ulong = packet_pending_bytes(pkt);
  if (read_size_ulong == 0)
    return pkt;

  uint64_t times_staled = 0;
  while (read_size_ulong > 0 && !msg->is_completed && !g_options.terminate) {
    if (read_size_ulong >= INT_MAX)
      goto cleanup;
    int read_size = (int)read_size_ulong;

    /* Pad read_size to multiple of usb's max packet size */
    read_size += (512 - (read_size % 512)) % 512;

    /* Expand buffer if needed */
    if (pkt->buffer_capacity < pkt->filled_size + read_size_ulong)
      if (packet_expand(pkt) < 0) {
	ERR("Failed to ensure room for usb pkt");
	goto cleanup;
      }

    int gotten_size = 0;
    int status = libusb_bulk_transfer(conn->parent->printer,
		                      conn->interface->endpoint_in,
		                      pkt->buffer + pkt->filled_size,
		                      read_size,
		                      &gotten_size, timeout);

    if (status == LIBUSB_ERROR_NO_DEVICE) {
      ERR("Printer has been disconnected");
      goto cleanup;
    }

    if (status != 0 && status != LIBUSB_ERROR_TIMEOUT) {
      ERR("bulk xfer failed with error code %d", status);
      ERR("tried reading %d bytes", read_size);
      goto cleanup;
    } else if (status == LIBUSB_ERROR_TIMEOUT) {
      ERR("bulk xfer timed out, retrying ...");
      ERR("tried reading %d bytes, actually read %d bytes",
	  read_size, gotten_size);
    }

    if (gotten_size < 0) {
      ERR("Negative read size unexpected");
      goto cleanup;
    }

    if (gotten_size > 0) {
      times_staled = 0;
      usb_conn_mark_moving(conn);
    } else {

      /* Performance Test ---------------
	 How long we sleep here has a
	 dramatic affect on how long it
	 takes to load a page.
	 Earlier versions waited a tenth
	 of a second which resulted in
	 minute long page loads.
	 On my HP printer the most obvious
	 bottleneck is the "Unified.js" file
	 which weighs 517.87KB. My profiling
	 looked at how shortening this sleep
	 could improve this file's load times.
	 The cycle count is from perf and
	 covers an entire page load.

	 Below are my results:
	 1 in    100 == 2447ms, 261M cycles
	 1 in  1,000 == 483ms,  500M cycles
	 5 in 10,000 == 433ms,  800M cycles
	 1 in 10,000 == 320ms, 3000M cycles */
      #define TIMEOUT_RATIO (10000 / 5)
      static uint64_t stale_timeout =
	CONN_STALE_THRESHHOLD * TIMEOUT_RATIO;
      static uint64_t crash_timeout =
	PRINTER_CRASH_TIMEOUT_ANSWER * TIMEOUT_RATIO;
      static uint64_t skip_timeout  =
	1000000000 / TIMEOUT_RATIO;

      struct timespec sleep_dur;
      sleep_dur.tv_sec = 0;
      sleep_dur.tv_nsec = skip_timeout;
      nanosleep(&sleep_dur, NULL);

      if (status == LIBUSB_ERROR_TIMEOUT)
	times_staled += TIMEOUT_RATIO * timeout / 1000;
      else
	times_staled++;
      if (times_staled % TIMEOUT_RATIO == 0 ||
	  status == LIBUSB_ERROR_TIMEOUT) {
	NOTE("No bytes received for %d sec.",
	     times_staled / TIMEOUT_RATIO);
	if (pkt->filled_size > 0)
	  NOTE("Packet so far \n===\n%s===\n",
	       hexdump(pkt->buffer,
		       pkt->filled_size));
      }
 
      if (times_staled > stale_timeout) {
	usb_conn_mark_staled(conn);

	if (pkt->filled_size > 0 ||
	    usb_all_conns_staled(conn->parent) ||
	    times_staled > crash_timeout) {
	  ERR("USB timed out, giving up waiting for more data");
	  break;
	}
      }
    }

    if (gotten_size) {
      NOTE("USB: Getting %d bytes of %d",
	   read_size, pkt->expected_size);
      NOTE("USB: Got %d bytes", gotten_size);
    }
    packet_mark_received(pkt, (size_t)gotten_size);
    read_size_ulong = packet_pending_bytes(pkt);
  }
  NOTE("USB: Received %d bytes of %d with type %d",
       pkt->filled_size, pkt->expected_size, msg->type);

  if (pkt->filled_size == 0)
    goto cleanup;

  return pkt;

 cleanup:
  if (pkt != NULL)
    packet_free(pkt);
  return NULL;
}
