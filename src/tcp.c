#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <fcntl.h>
#include <unistd.h>

#include "tcp.h"


tcp_sock_t *tcp_open(uint32_t port)
{
	tcp_sock_t *this = calloc(1, sizeof *this);
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

	// Let kernel over-accept HTTPMAXPENDCONNS number of connections
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

void tcp_close(tcp_sock_t *this)
{
	close(this->sd);
	free(this);
}

uint32_t get_port_number(tcp_sock_t *sock)
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

http_packet_t *get_packet(tcp_conn_t *tcp, http_message_t *msg)
{
	size_t capacity = BUFFER_STEP * BUFFER_INIT_RATIO;
	uint8_t *buf = malloc(capacity * (sizeof *buf));
	if (buf == NULL) {
		ERR("malloc failed for buf");
		goto error;
	}

	// Any portion of a packet left from last time?
	
	// Read until we have atleast one packet
	size_t size_read = recv(tcp->sd, buf, capacity, 0);

	// 5th case of http message end is when
	// client closes the connection.
	// Defined here:
	// http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
	// Note: this method will not be used by clients
	// if they expect a responce.
	if (size_read == 0) {
		msg->is_completed = TRUE;
	}
	
	// Did we receive more than a packets worth?
	
	http_packet_t *pkt = calloc(1, sizeof *pkt);
	if (pkt == NULL) {
		ERR("calloc failed for packet");
		goto error;
	}


	// Assemble packet
	pkt->buffer = buf;
	pkt->buffer_capacity = capacity;
	pkt->filled_size = size_read;
	pkt->parent_message = msg;

	sniff_request_type(pkt);

	return pkt;	
	 
error:
	if (buf != NULL)
		free(buf);
	if (pkt != NULL)
		free(pkt);
	return NULL;
}

http_message_t *tcp_message_get(tcp_conn_t *conn)
{
	http_message_t *msg = calloc(1, sizeof *msg);
	if (msg == NULL) {
		ERR("calloc failed on this");
		goto error;
	}

	return msg;

error:
	if (msg != NULL)
		free(msg);
	return NULL;
}

tcp_conn_t *tcp_conn_accept(tcp_sock_t *sock)
{
	tcp_conn_t *conn = calloc(1, sizeof *conn);
	if (conn == NULL) {
		ERR("Calloc for connection struct failed");
		goto error;
	}

	conn->sd = accept(sock->sd, NULL, NULL);
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

void tcp_conn_close(tcp_conn_t *conn)
{
	close(conn->sd);
	free(conn);
}

