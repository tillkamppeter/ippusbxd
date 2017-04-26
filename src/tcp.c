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
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "options.h"
#include "logging.h"
#include "tcp.h"


struct tcp_sock_t *tcp_open(uint16_t port, char* interface)
{
  struct tcp_sock_t *this = calloc(1, sizeof *this);
  if (this == NULL) {
    ERR("IPv4: callocing this failed");
    goto error;
  }

  // Open [S]ocket [D]escriptor
  this->sd = -1;
  this->sd = socket(AF_INET, SOCK_STREAM, 0);
  if (this->sd < 0) {
    ERR("IPv4 socket open failed");
    goto error;
  }
  // Set SO_REUSEADDR option to allow for a clean host/port unbinding even with
  // pending requests on shutdown of ippusbxd. Otherwise the port will stay
  // unavailable for a certain kernel-defined timeout. See also
  // http://stackoverflow.com/questions/10619952/how-to-completely-destroy-a-socket-connection-in-c
  int true = 1;
  if (setsockopt(this->sd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1) {
    ERR("IPv4 setting socket options failed");
    goto error;
  }

  // Find the IP address for the selected interface
  struct ifaddrs *ifaddr, *ifa;
  getifaddrs(&ifaddr);
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL)
      continue;
    if ((strcmp(ifa->ifa_name, interface) == 0) &&
	(ifa->ifa_addr->sa_family == AF_INET))
      break;
  }
  if (ifa == NULL) {
    ERR("Interface %s does not exist or IPv4 IP not found.", interface);
    goto error;
  }

  // Configure socket params
  struct sockaddr_in addr, *if_addr;
  if_addr = (struct sockaddr_in *) ifa->ifa_addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = if_addr->sin_addr.s_addr;
  //addr.sin_addr.s_addr = htonl(0xC0A8000F);
  NOTE("IPv4: Binding to %s:%d", inet_ntoa(if_addr->sin_addr), port);

  // Bind to the interface/IP/port
  if (bind(this->sd,
	   (struct sockaddr *)&addr,
	   sizeof addr) < 0) {
    if (g_options.only_desired_port == 1)
      ERR("IPv4 bind on port failed. "
	  "Requested port may be taken or require root permissions.");
    goto error;
  }

  // Let kernel over-accept max number of connections
  if (listen(this->sd, HTTP_MAX_PENDING_CONNS) < 0) {
    ERR("IPv4 listen failed on socket");
    goto error;
  }

  return this;

 error:
  if (this != NULL) {
    if (this->sd != -1) {
      close(this->sd);
    }
    free(this);
  }
  return NULL;
}

struct tcp_sock_t *tcp6_open(uint16_t port, char* interface)
{
  struct tcp_sock_t *this = calloc(1, sizeof *this);
  if (this == NULL) {
    ERR("IPv6: callocing this failed");
    goto error;
  }

  // Open [S]ocket [D]escriptor
  this->sd = -1;
  this->sd = socket(AF_INET6, SOCK_STREAM, 0);
  if (this->sd < 0) {
    ERR("Ipv6 socket open failed");
    goto error;
  }
  // Set SO_REUSEADDR option to allow for a clean host/port unbinding even with
  // pending requests on shutdown of ippusbxd. Otherwise the port will stay
  // unavailable for a certain kernel-defined timeout. See also
  // http://stackoverflow.com/questions/10619952/how-to-completely-destroy-a-socket-connection-in-c
  int true = 1;
  if (setsockopt(this->sd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1) {
    ERR("IPv6 setting socket options failed");
    goto error;
  }

  // Find the IP address for the selected interface
  struct ifaddrs *ifaddr, *ifa;
  getifaddrs(&ifaddr);
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL)
      continue;
    if ((strcmp(ifa->ifa_name, interface) == 0) &&
	(ifa->ifa_addr->sa_family == AF_INET6))
      break;
  }
  if (ifa == NULL) {
    ERR("Interface %s does not exist or IPv6 IP not found.", interface);
    goto error;
  }

  // Configure socket params
  struct sockaddr_in6 addr, *if_addr;
  char buf[64];
  if_addr = (struct sockaddr_in6 *) ifa->ifa_addr;
  memset(&addr, 0, sizeof addr);
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port);
  addr.sin6_addr = if_addr->sin6_addr;
  addr.sin6_scope_id=if_nametoindex(interface);
  if (inet_ntop(addr.sin6_family, (void *)&(addr.sin6_addr),
		buf, sizeof(buf)) == NULL) {
    ERR("Could not determine IPv6 IP address for interface %s.",
	interface);
    goto error;
  }
  NOTE("IPv6: Binding to [%s]:%d", buf, port);

  // Bind to the interface/IP/port
  if (bind(this->sd,
	   (struct sockaddr *)&addr,
	   sizeof addr) < 0) {
    if (g_options.only_desired_port == 1)
      ERR("IPv6 bind on port failed. "
	  "Requested port may be taken or require root permissions.");
    goto error;
  }

  // Let kernel over-accept max number of connections
  if (listen(this->sd, HTTP_MAX_PENDING_CONNS) < 0) {
    ERR("IPv6 listen failed on socket");
    goto error;
  }

  return this;

 error:
  if (this != NULL) {
    if (this->sd != -1) {
      close(this->sd);
    }
    free(this);
  }
  return NULL;
}

void tcp_close(struct tcp_sock_t *this)
{
  close(this->sd);
  free(this);
}

uint16_t tcp_port_number_get(struct tcp_sock_t *sock)
{
  sock->info_size = sizeof sock->info;
  int query_status = getsockname(sock->sd,
				 (struct sockaddr *) &(sock->info),
				 &(sock->info_size));
  if (query_status == -1) {
    ERR("query on socket port number failed");
    goto error;
  }

  return ntohs(sock->info.sin6_port);

 error:
  return 0;
}

struct http_packet_t *tcp_packet_get(struct tcp_conn_t *tcp,
                                     struct http_message_t *msg)
{
  // Alloc packet ==---------------------------------------------------==
  struct http_packet_t *pkt = packet_new(msg);
  if (pkt == NULL) {
    ERR("failed to create packet for incoming tcp message");
    goto error;
  }

  size_t want_size = packet_pending_bytes(pkt);
  if (want_size == 0) {
    NOTE("TCP: Got %lu from spare buffer", pkt->filled_size);
    return pkt;
  }

  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  setsockopt(tcp->sd, SOL_SOCKET, SO_RCVTIMEO,
	     (char *)&tv, sizeof(struct timeval));

  while (want_size != 0 && !msg->is_completed && !g_options.terminate) {
    NOTE("TCP: Getting %d bytes", want_size);
    uint8_t *subbuffer = pkt->buffer + pkt->filled_size;
    ssize_t gotten_size = recv(tcp->sd, subbuffer, want_size, 0);
    if (gotten_size < 0) {
      int errno_saved = errno;
      ERR("recv failed with err %d:%s", errno_saved,
	  strerror(errno_saved));
      tcp->is_closed = 1;
      goto error;
    }
    NOTE("TCP: Got %d bytes", gotten_size);
    if (gotten_size == 0) {
      tcp->is_closed = 1;
      if (pkt->filled_size == 0) {
	// Client closed TCP conn
	goto error;
      } else {
	break;
      }
    }

    packet_mark_received(pkt, (unsigned) gotten_size);
    want_size = packet_pending_bytes(pkt);
    NOTE("TCP: Want more %d bytes; Message %scompleted", want_size, msg->is_completed ? "" : "not ");
  }

  NOTE("TCP: Received %lu bytes", pkt->filled_size);
  return pkt;

 error:
  if (pkt != NULL)
    packet_free(pkt);
  return NULL;
}

int tcp_packet_send(struct tcp_conn_t *conn, struct http_packet_t *pkt)
{
  size_t remaining = pkt->filled_size;
  size_t total = 0;
  while (remaining > 0 && !g_options.terminate) {
    ssize_t sent = send(conn->sd, pkt->buffer + total,
			remaining, MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno == EPIPE) {
	conn->is_closed = 1;
	return 0;
      }
      ERR("Failed to sent data over TCP");
      return -1;
    }

    size_t sent_ulong = (unsigned) sent;
    total += sent_ulong;
    if (sent_ulong >= remaining)
      remaining = 0;
    else
      remaining -= sent_ulong;
  }
  NOTE("TCP: sent %lu bytes", total);
  return 0;
}


struct tcp_conn_t *tcp_conn_select(struct tcp_sock_t *sock,
				   struct tcp_sock_t *sock6)
{
  struct tcp_conn_t *conn = calloc(1, sizeof *conn);
  if (conn == NULL) {
    ERR("Calloc for connection struct failed");
    goto error;
  }
  fd_set rfds;
  int retval = 0;
  int nfds = 0;
  FD_ZERO(&rfds);
  if (sock) {
    FD_SET(sock->sd, &rfds);
    nfds = sock->sd;
  }
  if (sock6) {
    FD_SET(sock6->sd, &rfds);
    if (sock6->sd > nfds)
      nfds = sock6->sd;
  }
  if (nfds == 0) {
    ERR("No valid TCP socket supplied.");
    goto error;
  }
  nfds += 1;
  retval = select(nfds, &rfds, NULL, NULL, NULL);
  if (g_options.terminate)
    goto error;
  if (retval < 1) {
    ERR("Failed to open tcp connection");
    goto error;
  }
  if (sock && FD_ISSET(sock->sd, &rfds)) {
    conn->sd = accept(sock->sd, NULL, NULL);
    NOTE ("Using IPv4");
  } else if (sock6 && FD_ISSET(sock6->sd, &rfds)) {
    conn->sd = accept(sock6->sd, NULL, NULL);
    NOTE ("Using IPv6");
  } else {
    ERR("select failed");
    goto error;
  }
  if (conn->sd < 0) {
    ERR("accept failed");
    goto error;
  }
  return conn;

 error:
  if (conn != NULL)
    free(conn);
  return NULL;
}

void tcp_conn_close(struct tcp_conn_t *conn)
{
  // Unbind host/port cleanly even with pending requests. Otherwise
  // the port will stay unavailable for a certain kernel-defined
  // timeout. See also
  // http://stackoverflow.com/questions/10619952/how-to-completely-destroy-a-socket-connection-in-c
  shutdown(conn->sd, SHUT_RDWR);

  close(conn->sd);
  free(conn);
}
