#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "http.h"
#include "logging.h"

#define BUFFER_STEP (1 << 18)

struct http_message_t *http_message_new()
{
	struct http_message_t *msg = calloc(1, sizeof(*msg));
	if (msg == NULL) {
		ERR("failed to alloc space for http message");
		return NULL;
	}


	size_t capacity = BUFFER_STEP;
	msg->spare_buffer = calloc(capacity, sizeof(*(msg->spare_buffer)));
	if (msg->spare_buffer == NULL) {
		ERR("failed to alloc buffer for http message");
		free(msg);
		return NULL;
	}

	msg->spare_capacity = capacity;

	return msg;
}

void message_free(struct http_message_t *msg)
{
	free(msg->spare_buffer);
	free(msg);
}


static int doesMatch(const char *matcher, size_t matcher_len,
                     const uint8_t *key, size_t key_len)
{
	for (size_t i = 0; i < matcher_len; i++)
		if (i >= key_len || matcher[i] != key[i])
			return 0;
	return 1;
}

static int inspect_header_field(struct http_packet_t *pkt, size_t header_size,
                                char *key, size_t key_size)
{
	// Find key
	uint8_t *pos = memmem(pkt->buffer, header_size, key, key_size);
	if (pos == NULL)
		return -1;

	// Find first digit
	size_t number_pos = (pos - pkt->buffer) + key_size;
	while (number_pos < pkt->filled_size && !isdigit(pkt->buffer[number_pos]))
		++number_pos;

	// Find next non-digit
	size_t number_end = number_pos;
	while (number_end < pkt->filled_size && isdigit(pkt->buffer[number_end]))
		++number_end;

	// Failed to find next non-digit
	// header field might be broken
	if (number_end >= pkt->filled_size)
		return -1;

	// Temporary stringification of buffer for atoi()
	char original_char = pkt->buffer[number_end];
	pkt->buffer[number_end] = '\0';
	int val = atoi((const char *)(pkt->buffer + number_pos));

	// Restore buffer
	pkt->buffer[number_end] = original_char;
	return val;
}

// Warning: this functon should be reviewed indepth.
// Copying memory is a fantastic place to find exploits
// TODO: make this more agreesive with exiting on broken assumptions
// TODO: Expand msg spare buffer as needed
static void packet_store_excess(struct http_packet_t *pkt)
{
	struct http_message_t *msg = pkt->parent_message;
	if (pkt->expected_size >= pkt->filled_size) {
		ERR("Do not call packet_store_excess() unless needed");
		return;
	}
	size_t spare_size = pkt->filled_size - pkt->expected_size;

	// Note: Mesaages's spare buffer should be empty!
	assert(msg->spare_filled == 0);

	// Note: Packets cannot have more spare data than they have data.
	assert(pkt->filled_size >= spare_size);

	size_t non_spare = pkt->filled_size - spare_size;

	// Note: Packets cannot have more
	// than BUFFER_STEP of spare data
	assert(spare_size <= BUFFER_STEP);

	// TODO: expand msg's buffer
	if (spare_size > (msg->spare_capacity - msg->spare_filled)) {
		WARN("spare data exceeds message's buffer");
		exit(0);
	}

	// Note: We cannot copy past packet's buffer
	assert(pkt->buffer_capacity >= (non_spare + spare_size));

	// Warn: data can be lost if msg's buffer is too small
	memcpy(msg->spare_buffer, pkt->buffer + non_spare, spare_size);

	msg->spare_filled = spare_size;
	pkt->filled_size -= spare_size;
}

// Warning: this functon should be reviewed indepth.
// Copying memory is a fantastic place to find exploits
static void packet_load_spare(struct http_packet_t *pkt)
{
	struct http_message_t *msg = pkt->parent_message;
	if (msg->spare_filled == 0)
		return;

	size_t spare_avail = msg->spare_filled;
	size_t buffer_open = pkt->buffer_capacity - pkt->filled_size;
	assert(spare_avail <= buffer_open); // TODO: make this runtime error

	memcpy(pkt->buffer + pkt->filled_size,
	       msg->spare_buffer, spare_avail);

	msg->spare_filled = 0;
	pkt->filled_size += spare_avail;
}

static ssize_t packet_find_chunked_size(struct http_packet_t *pkt)
{
	// TODO: scan to the end of the chunk's
	// trailer and extensions

	// Find end of size string
	uint8_t *size_end = NULL;
	for (size_t i = 0; i < pkt->filled_size; i++) {
		uint8_t *buf = pkt->buffer;
		if (buf[i] == ';'  || // chunked extension
		    buf[i] == '\r' || // CR
		    buf[i] == '\n') { // Lf
			size_end = buf + i;
			break;
		}
	}

	if (size_end == NULL)
		return -1;

	// Temporary stringification for strtol()
	uint8_t original_char = *size_end;
	*size_end = '\0';
	size_t size = strtol((char *)pkt->buffer, NULL, 16);
	*size_end = original_char;

	// Chunked transport sends a zero size
	// chunk to mark end of message
	if (size == 0) {
		puts("Found end chunked packet");
		pkt->parent_message->is_completed = 1;
	}

	return size + (size_end - pkt->buffer);
}

static ssize_t packet_get_header_size(struct http_packet_t *pkt)
{
	// Find header
	for (size_t i = 0; i < pkt->filled_size; i++) {
		// two \r\n pairs
		if ((i + 3) < pkt->filled_size &&
		    '\r' == pkt->buffer[i] &&
		    '\n' == pkt->buffer[i + 1] &&
		    '\r' == pkt->buffer[i + 2] &&
		    '\n' == pkt->buffer[i + 3]) {
				return i + 4;
		}

		// two \n pairs
		if ((i + 1) < pkt->filled_size &&
		    '\n' == pkt->buffer[i] &&
		    '\n' == pkt->buffer[i + 1]) {
				return i + 2;
		}
	}

	return -1;
}

enum http_request_t packet_find_type(struct http_packet_t *pkt)
{
	enum http_request_t type = HTTP_UNSET;
	ssize_t size = 0;
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

	ssize_t header_size = packet_get_header_size(pkt);
	if (header_size < 0) {
		// We don't have the header yet
		goto do_ret;
	}

	// Try Transfer-Encoding Chunked
	char xfer_encode_str[] = "Transfer-Encoding: chunked";
	int xfer_encode_str_size = sizeof(xfer_encode_str) - 1;
	uint8_t *xfer_encode_pos = memmem(pkt->buffer, header_size,
	                                  xfer_encode_str,
	                                  xfer_encode_str_size);
	if (xfer_encode_pos != NULL) {
		size = 0;
		type = HTTP_CHUNKED;
		goto do_ret;
	}

	// Try Content-Length
	char content_length_str[] = "Content-Length: ";
	size = inspect_header_field(pkt, header_size, content_length_str,
	                            sizeof(content_length_str) - 1);
	if (size >= 0) {
		size += header_size;
		type = HTTP_CONTENT_LENGTH;
		goto do_ret;
	} 

	// Get requests
	if (doesMatch("GET", 3, pkt->buffer, pkt->filled_size)) {
		size = pkt->filled_size;
		type = HTTP_HEADER_ONLY;
		goto do_ret;
	}

	// No size was detectable yet header was found
	type = HTTP_UNKNOWN;

do_ret:
	pkt->parent_message->claimed_size = size;
	pkt->parent_message->type = type;
	return type;
}

// TODO: move this into the packet expander
int packet_at_capacity(struct http_packet_t *pkt)
{
	// Libusb requires atleast one usb packet's worth of free memory
	int USB_PACKET_SIZE = 512;
	return (pkt->buffer_capacity - USB_PACKET_SIZE) <= pkt->filled_size;
}

size_t packet_pending_bytes(struct http_packet_t *pkt)
{
	size_t pending = 0;
	// Determine message's size delimitation method
	struct http_message_t *msg = pkt->parent_message;
	if (HTTP_UNSET == msg->type) {
		msg->type = packet_find_type(pkt);

		if (HTTP_CHUNKED == msg->type) {
			// Note: this was the packet with the
			// header.

			// Save any non-header data we got
			ssize_t header_size = packet_get_header_size(pkt);
			if (header_size < 0 || (size_t)header_size < pkt->filled_size) {
				// Should not happen
				goto pending_known;
			}
			pkt->expected_size = header_size;
			pending = 0;
			goto pending_known;
		}
	}


	if (HTTP_CHUNKED == msg->type) {
		if (pkt->expected_size == 0) {
			ssize_t size = packet_find_chunked_size(pkt);
			if (size <= 0) {
				ERR("Malformed chunk-transport http packer receivd");
				exit(1);
			}
			pkt->expected_size = size;
		}

		pending = pkt->expected_size - pkt->filled_size;
		goto pending_known;
	}
	if (HTTP_HEADER_ONLY == msg->type) {
		// Note: we can only know it is header only
		// when the buffer already contains the header.
		pkt->expected_size = packet_get_header_size(pkt);
		pending = 0;
		goto pending_known;
	}
	if (HTTP_CONTENT_LENGTH == msg->type) {
		pkt->expected_size = msg->claimed_size;
		pending = msg->claimed_size - msg->received_size;
		goto pending_known;
	}

	// HTTP_UNKOWN or UNSET
	pending = pkt->buffer_capacity - pkt->filled_size;

pending_known:
	// Expand buffer as needed
	while (pending + pkt->filled_size > pkt->buffer_capacity) {
		ssize_t new_size = packet_expand(pkt);
		if (new_size < 0) {
			WARN("Failed to require space needed for message");
			return 0;
		}
	}
	// Save excess data
	if (pkt->expected_size != 0 && pkt->expected_size < pkt->filled_size)
		packet_store_excess(pkt);
	return pending;
}

void packet_mark_received(struct http_packet_t *pkt, size_t received)
{
	pkt->filled_size += received;

	struct http_message_t *msg = pkt->parent_message;
	msg->received_size += received;

	// Store excess data
	if (pkt->expected_size && pkt->filled_size > pkt->expected_size)
		packet_store_excess(pkt);
}

struct http_packet_t *packet_new(struct http_message_t *parent_msg)
{
	struct http_packet_t *pkt = NULL;
	uint8_t              *buf = NULL;
	size_t const capacity = BUFFER_STEP;

	assert(parent_msg != NULL);

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

	// Claim old spare data
	packet_load_spare(pkt);

	return pkt;
}

void packet_free(struct http_packet_t *pkt)
{
	free(pkt->buffer);
	free(pkt);
}

#define MAX_PACKET_SIZE (1 << (6 + 20)) // 64MiB
ssize_t packet_expand(struct http_packet_t *pkt)
{
	size_t cur_size = pkt->buffer_capacity;
	if (cur_size >= MAX_PACKET_SIZE) {
		return -1;
	}

	size_t new_size = cur_size * 2;
	uint8_t *new_buf = realloc(pkt->buffer, new_size);
	if (new_buf == NULL) {
		// If realloc fails the original buffer is still valid
		WARN("Failed to expand packet");
		return -1;
	}
	pkt->buffer = new_buf;
	return new_size - cur_size;
}
