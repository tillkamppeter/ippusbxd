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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>

#include "bonjour.h"
#include "logging.h"

/*
 * 'dnssd_callback()' - Handle Bonjour registration events.
 */

static void
dnssd_callback(
    AvahiEntryGroup      *srv,		/* I - Service */
    AvahiEntryGroupState state,		/* I - Registration state */
    void                 *context)	/* I - Printer */
{
  (void)srv;
  (void)state;
  (void)context;
}


/*
 * 'dnssd_client_cb()' - Client callback for Avahi.
 *
 * Called whenever the client or server state changes...
 */

static void
dnssd_client_cb(
    AvahiClient      *c,		/* I - Client */
    AvahiClientState state,		/* I - Current state */
    void             *userdata)		/* I - User data (unused) */
{
  (void)userdata;

  if (!c)
    return;

  switch (state)
  {
    default :
        NOTE("Ignore Avahi state %d.\n", state);
	break;

    case AVAHI_CLIENT_FAILURE:
	if (avahi_client_errno(c) == AVAHI_ERR_DISCONNECTED)
	  ERR_AND_EXIT("Avahi server crashed, exiting.\n");
  }
}

void
dnssd_init(bonjour_t *bonjour_data)
{
  int error;			/* Error code, if any */

  if ((bonjour_data->DNSSDMaster = avahi_threaded_poll_new()) == NULL)
    ERR_AND_EXIT("Error: Unable to initialize Bonjour.\n", stderr);

  if ((bonjour_data->DNSSDClient =
       avahi_client_new(avahi_threaded_poll_get(bonjour_data->DNSSDMaster),
			AVAHI_CLIENT_NO_FAIL,
			dnssd_client_cb, NULL, &error)) == NULL)
    ERR_AND_EXIT("Error: Unable to initialize Bonjour.\n", stderr);

  avahi_threaded_poll_start(bonjour_data->DNSSDMaster);
}

void
dnssd_shutdown(bonjour_t *bonjour_data) {
  avahi_threaded_poll_lock(bonjour_data->DNSSDMaster);
  if (bonjour_data->ipp_ref)
    avahi_entry_group_free(bonjour_data->ipp_ref);
  avahi_threaded_poll_unlock(bonjour_data->DNSSDMaster);
}

/*
 * 'register_printer()' - Register a printer object via Bonjour.
 */

int
register_printer(bonjour_t *bonjour_data,
		 char      *device_id,
		 char      *interface,
		 int       port)
{
  AvahiStringList *ipp_txt;             /* Bonjour IPP TXT record */
  char            temp[256];            /* Subtype service string */
  char            dnssd_name[1024];
  char            *dev_id = NULL;
  const char      *make;                /* I - Manufacturer */
  const char      *model;               /* I - Model name */
  const char      *serial = NULL;
  const char      *cmd;
  int             pwgraster = 0,
                  appleraster = 0,
                  pdf = 0,
                  jpeg = 0;
  char            formats[1024];        /* I - Supported formats */
  char            *ptr;
  int             error;

 /*
  * Parse the device ID for MFG, MDL, and CMD
  */

  dev_id = strdup(device_id);
  if ((ptr = strcasestr(dev_id, "MFG:")) == NULL)
    if ((ptr = strcasestr(dev_id, "MANUFACTURER:")) == NULL) {
      ERR("No manufacturer info in device ID");
      free(dev_id);
      return -1;
    }
  make = strchr(ptr, ':') + 1;
  if ((ptr = strcasestr(dev_id, "MDL:")) == NULL)
    if ((ptr = strcasestr(dev_id, "MODEL:")) == NULL) {
      ERR("No model info in device ID");
      free(dev_id);
      return -1;
    }
  model = strchr(ptr, ':') + 1;
  if ((ptr = strcasestr(dev_id, "SN:")) == NULL)
    if ((ptr = strcasestr(dev_id, "SERN:")) == NULL) {
      if ((ptr = strcasestr(dev_id, "SERIALNUMBER:")) == NULL) {
	NOTE("No serial number info in device ID");
      }
    }
  if (ptr)
    serial = strchr(ptr, ':') + 1;
  if ((ptr = strcasestr(dev_id, "CMD:")) == NULL)
    if ((ptr = strcasestr(dev_id, "COMMAND SET:")) == NULL) {
      ERR("No page description language info in device ID");
      free(dev_id);
      return -1;
    }
  cmd = strchr(ptr, ':') + 1;
  ptr = strchr(make, ';');
  if (ptr) *ptr = '\0';
  ptr = strchr(model, ';');
  if (ptr) *ptr = '\0';
  if (serial) {
    ptr = strchr(serial, ';');
    if (ptr) *ptr = '\0';
  }
  ptr = strchr(cmd, ';');
  if (ptr) *ptr = '\0';

  if ((ptr = strcasestr(cmd, "pwg")) != NULL &&
      (ptr = strcasestr(ptr, "raster")) != NULL)
    pwgraster = 1;
  if (((ptr = strcasestr(cmd, "apple")) != NULL &&
       (ptr = strcasestr(ptr, "raster")) != NULL) ||
      ((ptr = strcasestr(cmd, "urf")) != NULL))
    appleraster = 1;
  if ((ptr = strcasestr(cmd, "pdf")) != NULL)
    pdf = 1;
  if ((ptr = strcasestr(cmd, "jpeg")) != NULL ||
      (ptr = strcasestr(cmd, "jpg")) != NULL)
    jpeg = 1;
  snprintf(formats, sizeof(formats),"%s%s%s%s",
	   (pdf ? "application/pdf," : ""),
	   (pwgraster ? "image/pwg-raster," : ""),
	   (appleraster ? "image/urf," : ""),
	   (jpeg ? "image/jpeg," : ""));
  formats[strlen(formats) - 1] = '\0';

 /*
  * Additional printer properties
  */

  snprintf(temp, sizeof(temp), "http://localhost:%d/", port);
  if (serial)
    snprintf(dnssd_name, sizeof(dnssd_name), "%s [%s]", model, serial);
  else
    snprintf(dnssd_name, sizeof(dnssd_name), "%s", model);

 /*
  * Create the TXT record...
  */

  ipp_txt = NULL;
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "rp=ipp/print");
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "ty=%s %s", make, model);
  /* ipp_txt = avahi_string_list_add_printf(ipp_txt, "adminurl=%s", temp); */
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "product=(%s)", model);
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "pdl=%s", formats);
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "Color=U");
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "Duplex=U");
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "usb_MFG=%s", make);
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "usb_MDL=%s", model);
  if (appleraster)
    ipp_txt = avahi_string_list_add_printf(ipp_txt, "URF=CP1,IS1-5-7,MT1-2-3-4-5-6-8-9-10-11-12-13,RS300,SRGB24,V1.4,W8,DM1");
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "priority=60");
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "txtvers=1");
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "qtotal=1");
  free(dev_id);

  avahi_threaded_poll_lock(bonjour_data->DNSSDMaster);

 /*
  * Register _printer._tcp (LPD) with port 0 to reserve the service name...
  */

  bonjour_data->ipp_ref = avahi_entry_group_new(bonjour_data->DNSSDClient,
						dnssd_callback, NULL);
  error =
    avahi_entry_group_add_service_strlst(bonjour_data->ipp_ref,
					 (interface ?
					  (int)if_nametoindex(interface) :
					  AVAHI_IF_UNSPEC),
					 AVAHI_PROTO_UNSPEC, 0,
					 dnssd_name,
					 "_printer._tcp", NULL, NULL, 0,
					 NULL);
  if (error)
    ERR("Error registering %s as Unix printer (_printer._tcp): %d", dnssd_name,
	error);

 /*
  * Then register the _ipp._tcp (IPP)...
  */

  error =
    avahi_entry_group_add_service_strlst(bonjour_data->ipp_ref,
					 (interface ?
					  (int)if_nametoindex(interface) :
					  AVAHI_IF_UNSPEC),
					 AVAHI_PROTO_UNSPEC, 0,
					 dnssd_name,
					 "_ipp._tcp", NULL, NULL, port,
					 ipp_txt);
  if (error)
    ERR("Error registering %s as IPP printer (_ipp._tcp): %d", dnssd_name,
	error);
  else {
    error =
      avahi_entry_group_add_service_subtype(bonjour_data->ipp_ref,
					    (interface ?
					     (int)if_nametoindex(interface) :
					     AVAHI_IF_UNSPEC),
					    AVAHI_PROTO_UNSPEC, 0,
					    dnssd_name,
					    "_ipp._tcp", NULL,
					    (appleraster && !pwgraster ?
					     "_universal._sub._ipp._tcp" :
					     "_print._sub._ipp._tcp"));
    if (error)
      ERR("Error registering subtype for IPP printer %s (_print._sub._ipp._tcp or _universal._sub._ipp._tcp): %d", dnssd_name,
	  error);
  }

 /*
  * Finally _http.tcp (HTTP) for the web interface...
  */

  error =
    avahi_entry_group_add_service_strlst(bonjour_data->ipp_ref,
					 (interface ?
					  (int)if_nametoindex(interface) :
					  AVAHI_IF_UNSPEC),
					 AVAHI_PROTO_UNSPEC, 0,
					 dnssd_name,
					 "_http._tcp", NULL, NULL, port,
					 NULL);
  if (error)
    ERR("Error registering web interface of %s (_http._tcp): %d", dnssd_name,
	error);
  else {
    error =
      avahi_entry_group_add_service_subtype(bonjour_data->ipp_ref,
					    (interface ?
					     (int)if_nametoindex(interface) :
					     AVAHI_IF_UNSPEC),
					    AVAHI_PROTO_UNSPEC, 0,
					    dnssd_name,
					    "_http._tcp", NULL,
					    "_printer._sub._http._tcp");
    if (error)
      ERR("Error registering subtype for web interface of %s (_printer._sub._http._tcp): %d", dnssd_name,
	  error);
  }

 /*
  * Commit it...
  */

  avahi_entry_group_commit(bonjour_data->ipp_ref);
  avahi_threaded_poll_unlock(bonjour_data->DNSSDMaster);

  avahi_string_list_free(ipp_txt);

  return 0;
}

