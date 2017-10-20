/* Copyright (C) 2017 Daniel Dressler and contributors
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "http.h"

#define HTTP_MAX_PENDING_CONNS 0

struct uds_sock_t {
  int fd;
  struct sockaddr_un addr;
  socklen_t info_size;
};

struct uds_conn_t {
  int fd;
  int is_closed;
};

struct uds_sock_t *uds_open(const char *path);
void uds_close(struct uds_sock_t *sock);

struct uds_conn_t *uds_connect(struct uds_sock_t *sock);
void uds_conn_close(struct uds_conn_t *conn);

struct http_packet_t *uds_packet_get(struct uds_conn_t *conn,
                                     struct http_message_t *msg);

int uds_packet_send(struct uds_conn_t *conn, struct http_packet_t *pkt);
