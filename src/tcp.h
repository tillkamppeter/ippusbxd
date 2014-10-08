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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "http.h"

#define HTTP_MAX_PENDING_CONNS 0
#define BUFFER_STEP (1 << 13)
#define BUFFER_STEP_RATIO (2)
#define BUFFER_INIT_RATIO (1)
#define BUFFER_MAX (1 << 20)

struct tcp_sock_t {
	int sd;
	struct sockaddr_in6 info;
	socklen_t info_size;
};

struct tcp_conn_t {
	int sd;
	int is_closed;
};

struct tcp_sock_t *tcp_open(uint16_t);
void tcp_close(struct tcp_sock_t *);
uint16_t tcp_port_number_get(struct tcp_sock_t *);

struct tcp_conn_t *tcp_conn_accept(struct tcp_sock_t *);
void tcp_conn_close(struct tcp_conn_t *);

struct http_packet_t *tcp_packet_get(struct tcp_conn_t *,
                                     struct http_message_t *);
void tcp_packet_send(struct tcp_conn_t *, struct http_packet_t *);
