#pragma once
#include <stdint.h>
#include <sys/types.h>

enum http_request_t {
	HTTP_UNSET,
	HTTP_UNKNOWN,
	HTTP_CHUNKED,
	HTTP_CONTENT_LENGTH
};

struct http_message_t {
	enum http_request_t type;

	size_t spare_size;
	uint8_t spare_buf;

	uint32_t unreceived_size;
	uint32_t received_size;
	uint8_t is_completed;

};

struct http_packet_t {
	// size http headers claim for packet
	size_t claimed_size;

	// size of filled content
	size_t filled_size;

	// max capacity of buffer
	// can be exapanded
	size_t buffer_capacity;
	uint8_t *buffer;

	struct http_message_t *parent_message;
};

struct http_message_t *http_message_new(void);
void free_message(struct http_message_t *);

enum http_request_t sniff_request_type(struct http_packet_t *pkt);
struct http_packet_t *packet_new(struct http_message_t *);
void free_packet(struct http_packet_t *);
