#pragma once
#include <stdint.h>


#define ERR(x, ...) do { fprintf(stderr, "Error: " x, ##__VA_ARGS__); } while (0)

#define HTTPMAXPENDINGCONNS 5

typedef struct packet {
	size_t size;
	uint8_t *buffer;

	struct packet *next;
} packet;

typedef struct {
	size_t total_size;
	packet *packets;
} message;

typedef struct {
	int sd;
} http_handle;

http_handle *open_http();
void close_http();
