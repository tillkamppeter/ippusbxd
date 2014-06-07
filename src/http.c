#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "logging.h"
#include "http.h"

struct http_message_t *http_message_new()
{
	struct http_message_t *msg = calloc(1, sizeof *msg);
	if (msg == NULL) {
		ERR("failed to alloc space for http message");
		goto error;
	}

	return msg;

error:
	if (msg != NULL)
		free(msg);
	return NULL;
}


int inspect_header_field(struct http_packet_t *pkt, int header_end,
                         char *search_key, int key_size)
{
	uint8_t *pos = memmem(pkt->buffer, header_end, search_key, key_size);
	if (pos == NULL)
		return -1;

	uint32_t num_pos = (pos - pkt->buffer) + key_size;
	int32_t num_end = -1;
	uint32_t i;
	for (i = num_pos; i < pkt->filled_size; i++) {
		if (!isdigit(pkt->buffer[i]))
			continue;
		// Find first non-digit
		while (i < pkt->filled_size && !isdigit(pkt->buffer[i])) {
			i++;
		}
		num_end = i;
	}
	if (num_end < 0) {
		return -1;
	}

	// Temporary stringification of buffer for atoi()
	char original_char = pkt->buffer[num_end];
	pkt->buffer[num_end] = '\0';
	int val = atoi((const char *)pkt->buffer + num_pos);
	pkt->buffer[num_end] = original_char;
	return val;
}



enum http_request_t sniff_request_type(struct http_packet_t *pkt)
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
	uint32_t i;
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

	char xfer_encode_str[] = "Transfer-Encoding: ";
	int size = inspect_header_field(pkt, header_end, xfer_encode_str,
	                                                sizeof xfer_encode_str);
	if (size >= 0) {
		type = HTTP_CHUNKED;
		pkt->claimed_size = size;
		goto do_ret;
	}

	char content_length_str[] = "Content-Length: ";
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


void free_message(struct http_message_t *msg)
{
	free(msg);
}

#define BUFFER_STEP (1 << 13)
#define BUFFER_STEP_RATIO (2)
#define BUFFER_INIT_RATIO (1)
#define BUFFER_MAX (1 << 20)
struct http_packet_t *packet_new(struct http_message_t *parent_msg)
{
	struct http_packet_t *pkt = NULL;
	uint8_t              *buf = NULL;
	size_t capacity = BUFFER_STEP * BUFFER_INIT_RATIO;

	buf = calloc(capacity, sizeof(*buf));
	if (buf == NULL) {
		ERR("failed to alloc space for packet's buffer");
		goto error;
	}

	pkt = calloc(1, sizeof(*pkt));
	if (pkt == NULL) {
		ERR("failed to alloc space for packet");
		goto error;
	}


	// Assemble packet
	pkt->buffer = buf;
	pkt->buffer_capacity = capacity;
	pkt->filled_size = 0;
	pkt->parent_message = parent_msg;


	// TODO: check if parent_message has excess data from
	// last time. If so move that into our buffer.

	return pkt;

error:
	if (buf != NULL)
		free(buf);
	if (pkt != NULL)
		free(pkt);
	return NULL;

}

void free_packet(struct http_packet_t *pkt)
{
	free(pkt->buffer);
	free(pkt);
}
