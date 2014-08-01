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

	msg->spare_capacity = 0;
	msg->spare_filled = 0;
	msg->spare_buffer = NULL;

	return msg;
}

void message_free(struct http_message_t *msg)
{
	free(msg->spare_buffer);
	free(msg);
}

static void packet_check_completion(struct http_packet_t *pkt)
{
	struct http_message_t *msg = pkt->parent_message;
	// Msg full
	if (msg->claimed_size && msg->received_size >= msg->claimed_size) {
		msg->is_completed = 1;

		// Sanity check
		if (msg->spare_filled > 0)
			ERR_AND_EXIT("Msg spare not empty upon completion");
	}

	// Pkt full
	if (pkt->expected_size && pkt->filled_size >= pkt->expected_size)
		pkt->is_completed = 1;

	// Pkt at capacity
	if (pkt->filled_size == pkt->buffer_capacity) {
		pkt->is_completed = 1;
		msg->is_completed = 1;
	} else if (pkt->filled_size > pkt->buffer_capacity) {
		// Santiy check
		ERR_AND_EXIT("Overflowed packet buffer");
	}
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

static void packet_store_excess(struct http_packet_t *pkt)
{
	struct http_message_t *msg = pkt->parent_message;
	if (msg->spare_buffer != NULL)
		ERR_AND_EXIT("Do not store excess to non-empty packet");

	if (pkt->expected_size >= pkt->filled_size)
		ERR_AND_EXIT("Do not call packet_store_excess() unless needed");

	size_t spare_size = pkt->filled_size - pkt->expected_size;
	size_t non_spare = pkt->expected_size;
	NOTE("HTTP: Storing %d bytes of excess", spare_size);

	// Align to BUFFER_STEP
	size_t needed_size = 0;
	needed_size += spare_size / BUFFER_STEP;
	needed_size += (spare_size % BUFFER_STEP) > 0 ? BUFFER_STEP : 0;

	if (msg->spare_buffer == NULL) {
		uint8_t *buffer = calloc(1, needed_size);
		if (buffer == NULL)
			ERR_AND_EXIT("Failed to alloc msg spare buffer");

		msg->spare_buffer = buffer;
	}

	memcpy(msg->spare_buffer, pkt->buffer + non_spare, spare_size);
	pkt->filled_size = non_spare;

	msg->spare_capacity = needed_size;
	msg->spare_filled = spare_size;
}

static void packet_take_spare(struct http_packet_t *pkt)
{
	struct http_message_t *msg = pkt->parent_message;
	if (msg->spare_filled == 0)
		return;

	if (msg->spare_buffer == NULL)
		return;

	if (pkt->filled_size > 0)
		ERR_AND_EXIT("pkt should be empty when loading msg spare");

	// Take message's buffer
	size_t msg_size = msg->spare_capacity;
	size_t msg_filled = msg->spare_filled;
	uint8_t *msg_buffer = msg->spare_buffer;

	pkt->buffer_capacity = msg_size;
	pkt->filled_size = msg_filled;
	pkt->buffer = msg_buffer;

	msg->spare_capacity = 0;
	msg->spare_filled = 0;
	msg->spare_buffer = NULL;
}

static ssize_t packet_find_chunked_size(struct http_packet_t *pkt)
{
	// TODO: support trailers
	// NOTE:
	// chunks can have trailers which are
	// tacked on http header fields. 
	// NOTE:
	// chunks may also have extensions.
	// No one uses or supports them.

	// Find end of size string
	uint8_t *size_end = NULL;
	uint8_t *miniheader_end = NULL;
	size_t max = pkt->filled_size;
	for (size_t i = 0; i < pkt->filled_size; i++) {
		uint8_t *buf = pkt->buffer;
		if (size_end == NULL) {
			// No extension
			if (i + 1 < max && (
				buf[i] == '\r' &&  // CR
				buf[i + 1] == '\n')// LF
			) {
				size_end = buf + i + 1;
				miniheader_end = size_end;
				break;
			}

			// No extension
			if (buf[i] == '\n') // LF
			{
				size_end = buf + i;
				miniheader_end = size_end;
				break;
			}
			
			// Has extensions
			if (buf[i] == ';')
			{
				size_end = buf + i;
				continue;
			}
		}

		if (miniheader_end == NULL) {
			if (i + 1 < max && (
				buf[i] == '\r' &&  // CR
				buf[i + 1] == '\n')// LF
			) {
				miniheader_end = buf + i + 1;
				break;
			}

			if (buf[i] == '\n') // LF
			{
				miniheader_end = buf + i;
				break;
			}
		}
	}

	if (miniheader_end == NULL) {
		// NOTE: knowing just the size field
		// is not enough since the extensions
		// are not included in the size
		NOTE("failed to find chunk mini-header so far");
		return -1;
	}



	// TODO: make copy instead of mangling
	// Temporary stringification for strtol()
	uint8_t original_char = *size_end;
	*size_end = '\0';
	size_t size = strtol((char *)pkt->buffer, NULL, 16);
	NOTE("Chunk size raw: %s", pkt->buffer);
	*size_end = original_char;

	// zero size chunk marks end of message
	if (size == 0) {
		NOTE("Found end chunked packet");
		pkt->parent_message->is_completed = 1;
		pkt->is_completed = 1;
	}

	size_t miniheader_size = miniheader_end - pkt->buffer + 1;

	size_t chunk_size = size; // Chunk body
	chunk_size += miniheader_size; // Mini-header
	chunk_size += 2; // Trailing CRLF
	NOTE("HTTP: Chunk size: %lu", chunk_size);
	return chunk_size;
}

static ssize_t packet_get_header_size(struct http_packet_t *pkt)
{
	if (pkt->header_size != 0)
		goto found;

	/* RFC2616 recomends we match newline on \n despite full
	 * complience requires the message to use only \r\n
	 * http://www.w3.org/Protocols/rfc2616/rfc2616-sec19.html#sec19.3
	 */

	// Find header
	for (size_t i = 0; i < pkt->filled_size; i++) {
		// two \r\n pairs
		if ((i + 3) < pkt->filled_size &&
		    '\r' == pkt->buffer[i] &&
		    '\n' == pkt->buffer[i + 1] &&
		    '\r' == pkt->buffer[i + 2] &&
		    '\n' == pkt->buffer[i + 3]) {
				pkt->header_size = i + 4;
				goto found;
		}

		// two \n pairs
		if ((i + 1) < pkt->filled_size &&
		    '\n' == pkt->buffer[i] &&
		    '\n' == pkt->buffer[i + 1]) {
				pkt->header_size = i + 2;
				goto found;
		}
	}

	return -1;

found:
	return pkt->header_size;
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

size_t packet_pending_bytes(struct http_packet_t *pkt)
{
	// Check Cache
	if (pkt->expected_size > 0)
		goto pending_known;

	struct http_message_t *msg = pkt->parent_message;

	if (HTTP_UNSET == msg->type) {
		msg->type = packet_find_type(pkt);

		if (HTTP_CHUNKED == msg->type) {
			// Note: this was the packet with the
			// header of our chunked message.

			// Save any non-header data we got
			ssize_t header_size = packet_get_header_size(pkt);

			// Sanity check
			if (header_size < 0 ||
			    (size_t)header_size > pkt->filled_size)
				ERR_AND_EXIT("HTTP: Could not find header twice");

			NOTE("HTTP: Chunked header size is %ld bytes",
				header_size);
			pkt->expected_size = header_size;
			msg->claimed_size = 0;
			goto pending_known;
		}
	}


	if (HTTP_CHUNKED == msg->type) {
		if (pkt->filled_size == 0) {
			// Grab chunk's mini-header
			goto pending_known;
		}

		if (pkt->expected_size == 0) {
			// Check chunk's mini-header
			ssize_t size = packet_find_chunked_size(pkt);
			if (size <= 0) {
				ERR("=============================================");
				ERR("Malformed chunk-transport http header receivd");
				ERR("Missing chunk's mini-headers in first data");
				ERR("Have %d bytes", pkt->filled_size);
				printf("%.*s\n", (int)pkt->filled_size, pkt->buffer);
				ERR("Malformed chunk-transport http header receivd");
				ERR("=============================================");
				goto pending_known;
			}

			pkt->expected_size = size;
			msg->claimed_size = 0;
		}

		goto pending_known;
	}
	if (HTTP_HEADER_ONLY == msg->type) {
		// Note: we can only know it is header only
		// when the buffer already contains the header.
		pkt->expected_size = packet_get_header_size(pkt);
		msg->claimed_size = pkt->expected_size;
		goto pending_known;
	}
	if (HTTP_CONTENT_LENGTH == msg->type) {
		// Note: find_header() has
		// filled msg's claimed_size
		msg->claimed_size = msg->claimed_size;
		pkt->expected_size = msg->claimed_size;
		goto pending_known;
	}

pending_known:

	// Save excess data
	if (pkt->expected_size && pkt->filled_size > pkt->expected_size)
		packet_store_excess(pkt);

	size_t expected = pkt->expected_size;
	if (expected == 0)
		expected = msg->claimed_size;
	if (expected == 0)
		expected = pkt->buffer_capacity;

	// Sanity check
	if (expected < pkt->filled_size)
		ERR_AND_EXIT("Expected cannot be larger than filled");

	size_t pending = expected - pkt->filled_size;
	
	packet_check_completion(pkt);

	// Expand buffer as needed
	while (pending + pkt->filled_size > pkt->buffer_capacity) {
		ssize_t size_added = packet_expand(pkt);
		if (size_added < 0) {
			WARN("packet at max allowed size");
			return 0;
		}
		if (size_added == 0) {
			ERR("Failed to expand packet");
			return 0;
		}
	}

	return pending;
}

void packet_mark_received(struct http_packet_t *pkt, size_t received)
{
	struct http_message_t *msg = pkt->parent_message;
	msg->received_size += received;

	pkt->filled_size += received;
	NOTE("HTTP: got %lu bytes so: pkt has %lu bytes, msg has %lu bytes",
		received, pkt->filled_size, msg->received_size);

	packet_check_completion(pkt);

	if (pkt->filled_size > pkt->buffer_capacity)
		ERR_AND_EXIT("Overflowed packet's buffer");

	if (pkt->expected_size && pkt->filled_size > pkt->expected_size) {
		// Store excess data
		packet_store_excess(pkt);
	}
}

struct http_packet_t *packet_new(struct http_message_t *parent_msg)
{
	struct http_packet_t *pkt = NULL;
	uint8_t              *buf = NULL;
	size_t const capacity = BUFFER_STEP;

	assert(parent_msg != NULL);
	pkt = calloc(1, sizeof(*pkt));
	if (pkt == NULL) {
		ERR("failed to alloc packet");
		return NULL;
	}
	pkt->parent_message = parent_msg;
	pkt->expected_size = 0;

	// Claim any spare data from prior packets
	packet_take_spare(pkt);

	if (pkt->buffer == NULL) {
		buf = calloc(capacity, sizeof(*buf));
		if (buf == NULL) {
			ERR("failed to alloc space for packet's buffer or space for packet");
			free(pkt);
			return NULL;
		}

		// Assemble packet
		pkt->buffer = buf;
		pkt->buffer_capacity = capacity;
		pkt->filled_size = 0;
	}

	return pkt;
}

void packet_free(struct http_packet_t *pkt)
{
	free(pkt->buffer);
	free(pkt);
}

#define MAX_PACKET_SIZE (1 << 26) // 64MiB
ssize_t packet_expand(struct http_packet_t *pkt)
{
	size_t cur_size = pkt->buffer_capacity;
	size_t new_size = cur_size * 2;
	if (new_size > MAX_PACKET_SIZE)
		return -1;

	uint8_t *new_buf = realloc(pkt->buffer, new_size);
	if (new_buf == NULL) {
		// If realloc fails the original buffer is still valid
		WARN("Failed to expand packet");
		return 0;
	}
	pkt->buffer = new_buf;
	return new_size - cur_size;
}
