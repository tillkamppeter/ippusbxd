#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include "http.h"


http_sock *open_http()
{
	http_sock *this = calloc(1, sizeof *this);
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
	addr.sin6_port = htons(0); // let kernel decide
	addr.sin6_addr = in6addr_any;

	// Bind to localhost
	if (bind(this->sd,
	        (struct sockaddr *)&addr,
	        sizeof addr) < 0) {
		ERR("bind failed");
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

void close_http(http_sock *this)
{
	close(this->sd);
	free(this);
}

uint32_t get_port_number(http_sock *sock)
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

int inspect_header_field(packet *pkt, int header_end, char *search_key, int key_size)
{
	uint8_t *pos = memmem(pkt->buffer, header_end, search_key, key_size);
	if (pos != NULL) {
		int num_pos = (pos - pkt->buffer) + key_size;
		int num_end = -1;
		int i;
		for (i = num_pos; i < pkt->filled_size; i++) {
			if (isdigit(pkt->buffer[i])) {
				// Find first non-digit
				while (i < pkt->filled_size && !isdigit(pkt->buffer[i])) {
					i++;
				}
				num_end = i;
			}
		}
		if (num_end < 0) {
			return -1;
		}

		// Stringify buffer for atoi()
		char original_char = pkt->buffer[num_end];
		pkt->buffer[num_end] = '\0';
		int val = atoi(pkt->buffer + num_pos);
		pkt->buffer[num_end] = original_char;
		return val;
	}
	return -1;
}

enum http_request_t sniff_request_type(packet *pkt)
{
	enum http_request_t type;
	/* Valid methods for determining http request
	 * size are defined by W3 in RFC2616 section 4.4
	 * link: http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
	 */

	/* This function attempts to find what method this
	 * packet would use. This is only possible in specific case:
	 * 1. if the request uses method 1 we can check the http 
	 *    request type. We must be called on a packet which
	 *    has the full header.
	 * 2. if the request uses method 2 we need the full header
	 *    but a simple network-byte-order-aware string search
	 *    works. This function does not work if called with
	 *    a chunked transport's sub-packet.
	 * 3. if the request uses method 3 we again perform the
	 *    string search.
	 * 
	 * All cases require the packat to contain the full header.
	 */

	/* RFC2616 recomends we match newline on \n despite full
	 * complience requires the message to use only \r\n
	 * http://www.w3.org/Protocols/rfc2616/rfc2616-sec19.html#sec19.3
	 */
	// Find header
	int header_end = -1;
	int i;
	for (i = 0; i < pkt->filled_size; i++) {
		// two \r\n pairs
		if ((i + 3) < pkt->filled_size &&
		    '\r' == pkt->buffer[i] &&
		    '\n' == pkt->buffer[i + 1] &&
		    '\r' == pkt->buffer[i + 2] &&
		    '\n' == pkt->buffer[i + 3])
				header_end = i;

		// two \n pairs
		if ((i + 1) < pkt->filled_size &&
		    '\n' == pkt->buffer[i] &&
		    '\n' == pkt->buffer[i + 1])
				header_end = i;
	}
	if (header_end < 0) {
		// We don't have the header yet
		type = HTTP_UNSET;
		goto do_ret;
	}

	char *xfer_encode_str = "Transfer-Encoding: ";
	int size = inspect_header_field(pkt, header_end, xfer_encode_str,
	                                                   sizeof xfer_encode_str);
	if (size >= 0) {
		type = HTTP_CHUNKED;
		pkt->claimed_size = size;
		goto do_ret;
	}

	char *content_length_str = "Content-Length: ";
	size = inspect_header_field(pkt, header_end, content_length_str,
	                                                sizeof content_length_str);
	if (size >= 0) {
		type = HTTP_CONTENT_LENGTH;
		pkt->claimed_size = size;
		goto do_ret;
	}


do_ret:
	pkt->parent_message->type = type;
	return type;
}

packet *get_packet(message *msg)
{
	size_t capacity = BUFFER_STEP * BUFFER_INIT_RATIO;
	uint8_t *buf = malloc(capacity * (sizeof *buf));
	if (buf == NULL) {
		ERR("malloc failed for buf");
		goto error;
	}

	// Any portion of a packet left from last time?
	
	// Read until we have atleast one packet
	size_t size_read = recv(msg->parent_session->sd, buf, capacity, 0);

	// 5th case of http message end is when
	// client closes the connection.
	// Defined here: http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
	// Note: this method will not be used by clients
	// if they expect a responce.
	if (size_read == 0) {
		msg->is_completed = TRUE;
	}
	
	// Did we receive more than a packets worth?
	
	packet *pkt = calloc(1, sizeof *pkt);
	if (pkt == NULL) {
		ERR("calloc failed for packet");
		goto error;
	}

	// Assemble packet
	pkt->buffer = buf;
	pkt->buffer_capacity = capacity;
	pkt->filled_size = size_read;
	pkt->parent_message = msg;
	return pkt;	
	 
error:
	if (buf != NULL)
		free(buf);
	if (pkt != NULL)
		free(pkt);
	return NULL;
}

void free_packet(packet *pkt)
{
	free(pkt->buffer);
	free(pkt);
}


message *get_message(http_conn *conn)
{
	message *msg = calloc(1, sizeof *msg);
	if (msg == NULL) {
		ERR("calloc failed on this");
		goto error;
	}
	msg->parent_session = conn;

	return msg;

error:
	if (msg != NULL)
		free(msg);
	return NULL;
}

void free_message(message *msg)
{
	free(msg);
}


http_conn *accept_conn(http_sock *sock)
{
	http_conn *conn = calloc(1, sizeof *conn);
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

void close_conn(http_conn *conn)
{
	close(conn->sd);
	free(conn);
}

