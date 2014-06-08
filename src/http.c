#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "http.h"
#include "logging.h"


struct http_message_t *http_message_new()
{
	struct http_message_t *msg = calloc(1, sizeof *msg);
	if (msg == NULL) {
		ERR("failed to alloc space for http message");
	}
	return msg;
}


// TODO: This function doesn't work if
// - the last digit in the value is at the end of the buffer
// - if the value string is malformed for atoi
// - the value is -1
static int inspect_header_field(struct http_packet_t *pkt, size_t header_end,
                         char *search_key, size_t key_size)
{
	uint8_t *pos = memmem(pkt->buffer, header_end, search_key, key_size);
	if (pos == NULL)
		return -1;

	size_t num_pos = (pos - pkt->buffer) + key_size;
	size_t num_end = num_pos;
    
    // The start of our value should not be past our buffer
    assert(num_pos < pkt->filled_size);
    
    // find the first digit after the start.
    while (num_end < pkt->filled_size && !isdigit(pkt->buffer[num_end])) {
        ++num_end;
    }
    // find the first non-digit
    while (num_end < pkt->filled_size && isdigit(pkt->buffer[num_end])) {
        ++num_end;
    }
    // we didn't find a first digit or we didn't find a last non-digit
    if (num_end >= pkt->filled_size) {
        return -1;
    }

	// Temporary stringification of buffer for atoi()
	char original_char = pkt->buffer[num_end];
	pkt->buffer[num_end] = '\0';
	int val = atoi((const char *)(pkt->buffer + num_pos));
	pkt->buffer[num_end] = original_char;
	return val;
}



enum http_request_t sniff_request_type(struct http_packet_t *pkt)
{
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
		pkt->parent_message->type = HTTP_UNSET;
		return HTTP_UNSET;
	}

	// Try Transfer-Encoding
	char xfer_encode_str[] = "Transfer-Encoding: ";
	int size = inspect_header_field(pkt, header_end, xfer_encode_str,
	                                                sizeof xfer_encode_str);
	if (size >= 0) {
		pkt->claimed_size = size;
        pkt->parent_message->type = HTTP_CHUNKED;
        return HTTP_CHUNKED;
	}

	// Try Content-Length
	char content_length_str[] = "Content-Length: ";
	size = inspect_header_field(pkt, header_end, content_length_str,
	                                              sizeof content_length_str);
	if (size >= 0) {
		pkt->claimed_size = size;
		pkt->parent_message->type = HTTP_CONTENT_LENGTH;
        return HTTP_CONTENT_LENGTH;
	} 
    
    // TODO
    // not sure what to do in this case.
    assert(0);
    pkt->parent_message->type = HTTP_UNKNOWN;
    return HTTP_UNKNOWN;
}


void free_message(struct http_message_t *msg)
{
	free(msg);
}

#define BUFFER_STEP (1 << 13)
#define BUFFER_INIT_RATIO (1)
struct http_packet_t *packet_new(struct http_message_t *parent_msg)
{
	struct http_packet_t *pkt = NULL;
	uint8_t              *buf = NULL;
	size_t const capacity = BUFFER_STEP * BUFFER_INIT_RATIO;

	buf = calloc(capacity, sizeof(*buf));
    pkt = calloc(1, sizeof(*pkt));
	if (buf == NULL || pkt == NULL) {
		ERR("failed to alloc space for packet's buffer or space for packet");
        free(pkt);
        free(buf);
		return NULL;
	}
	
	// Assemble packet
	pkt->buffer = buf;
	pkt->buffer_capacity = capacity;
	pkt->filled_size = 0;
	pkt->parent_message = parent_msg;

	// TODO: check if parent_message has excess data from
	// last time. If so move that into our buffer.
	return pkt;
}

void free_packet(struct http_packet_t *pkt)
{
    if (pkt) {
        free(pkt->buffer);
        free(pkt);
    }
}
