#pragma once
#include <stdint.h>


#define ERR(x, ...) do { fprintf(stderr, "Error: " x "\n", ##__VA_ARGS__); } while (0)

#define HTTP_MAX_PENDING_CONNS 5
#define BUFFER_STEP (1 << 13)
#define BUFFER_STEP_RATIO (2)
#define BUFFER_INIT_RATIO (1)
#define BUFFER_MAX (1 << 20)

typedef struct {
	int sd;
} http_sock;

typedef struct {
	int sd;
} http_conn;

typedef struct {
	size_t spare_size;
	uint8_t spare_buf;
	http_conn *parent_session;
} message;

typedef struct packet {
	size_t size;
	uint8_t *buffer;
	message *parent_message;
} packet;

http_sock *open_http();
http_conn *accept_conn(http_sock *);
message *get_message(http_conn *);
packet *get_packet(message *);
void close_http(http_sock *);
