#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <fcntl.h>
#include <unistd.h>

#include "logging.h"
#include "tcp.h"


struct tcp_sock_t *tcp_open(uint32_t port)
{
	struct tcp_sock_t *this = calloc(1, sizeof *this);
	if (this == NULL) {
		ERR("callocing this failed");
		goto error;
	}

	// Open [S]ocket [D]escriptor
	this->sd = -1;
	this->sd = socket(AF_INET6, SOCK_STREAM, 0);
	if (this->sd < 0) {
		ERR("sockect open failed");
		goto error;
	}

	// Configure socket params
	struct sockaddr_in6 addr;
	memset(&addr, 0, sizeof addr);
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(port);
	addr.sin6_addr = in6addr_any;

	// Bind to localhost
	if (bind(this->sd,
	        (struct sockaddr *)&addr,
	        sizeof addr) < 0) {
		ERR("Bind on port failed. "
		    "Requested port may be taken or require root permissions.");
		goto error;
	}

	// Let kernel over-accept max number of connections
	if (listen(this->sd, HTTP_MAX_PENDING_CONNS) < 0) {
		ERR("listen failed on socket");
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

uint32_t tcp_port_number_get(struct tcp_sock_t *sock)
{
	sock->info_size = sizeof sock->info;
	int query_status = getsockname(
	                               sock->sd,
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
	// Ensure data available ==------------------------------------------==
	uint8_t ignored_data;
	ssize_t has_data = recv(tcp->sd, &ignored_data, 1, MSG_PEEK);
	if (has_data < 0) {
		return NULL;
	} else if (has_data == 0) {
		// TODO: mark closed socket
		tcp->is_closed = 1;
	}

	// Fill packet ==----------------------------------------------------==
	struct http_packet_t *pkt = packet_new(msg);
	if (pkt == NULL) {
		ERR("failed to create packet for incoming tcp message");
		goto error;
	}

	// Read until we have atleast one packet
	ssize_t size_read = recv(tcp->sd, pkt->buffer, pkt->buffer_capacity, 0);
	if (size_read <= 0) {
		ERR("data known in socket yet none present");
		goto error;
	}

	packet_mark_received(pkt, size_read);

	return pkt;	
	 
error:
	if (pkt != NULL)
		packet_free(pkt);
	return NULL;
}

void tcp_packet_send(struct tcp_conn_t *conn, struct http_packet_t *pkt)
{
	send(conn->sd, pkt->buffer, pkt->filled_size, 0);
}


struct tcp_conn_t *tcp_conn_accept(struct tcp_sock_t *sock)
{
	struct tcp_conn_t *conn = calloc(1, sizeof *conn);
	if (conn == NULL) {
		ERR("Calloc for connection struct failed");
		goto error;
	}

	conn->sd = accept(sock->sd, NULL, NULL);
	if (conn->sd < 0) {
		ERR("accept failed");
		goto error;
	}

	// Make socket non-blocking
	int flags = fcntl(conn->sd, F_GETFL, 0);
	if (flags < 0) {
		ERR("could not get socket's flags");
		goto error;
	}
	flags |= O_NONBLOCK;
	int status = fcntl(conn->sd, F_SETFL, flags);
	if (status != 0) {
		ERR("failed to set socket to non-blocking");
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
	close(conn->sd);
	free(conn);
}

