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

#pragma once
#include <stdint.h>
#include <pthread.h>

#include "dnssd.h"

enum log_target {
  LOGGING_STDERR,
  LOGGING_SYSLOG
};

struct options {
  /* Runtime configuration */
  uint16_t desired_port;
  int only_desired_port;
  uint16_t real_port;
  char *interface;
  char *unix_socket_path;
  enum log_target log_destination;

  /* Behavior */
  int help_mode;
  int verbose_mode;
  int nofork_mode;
  int noprinter_mode;
  int nobroadcast;
  int unix_socket_mode;

  /* Printer identity */
  unsigned char *serial_num;
  int vendor_id;
  int product_id;
  int bus;
  int device;
  char *device_id;

  /* Global variables */
  int terminate;
  dnssd_t *dnssd_data;
  pthread_t usb_event_thread_handle;
  struct tcp_sock_t *tcp_socket;
  struct tcp_sock_t *tcp6_socket;
  struct uds_sock_t *uds_socket;
};

extern struct options g_options;
