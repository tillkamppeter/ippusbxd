#pragma once
#include <stdint.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ERR(x, ...) do { fprintf(stderr, "Error: " x "\n", ##__VA_ARGS__); } while (0)
#define TRUE 1
#define FALSE 0

#define HTTP_MAX_PENDING_CONNS 5
#define BUFFER_STEP (1 << 13)
#define BUFFER_STEP_RATIO (2)
#define BUFFER_INIT_RATIO (1)
#define BUFFER_MAX (1 << 20)

typedef struct {
	int sd;
	struct sockaddr_in6 info;
	socklen_t info_size;
} http_sock;

typedef struct {
	int sd;
} http_conn;

enum http_request_t {
	HTTP_UNSET,
	HTTP_UNKNOWN,
	HTTP_CHUNKED,
	HTTP_CONTENT_LENGTH
};

typedef struct {
	enum http_request_t type;

	size_t spare_size;
	uint8_t spare_buf;

	uint32_t unreceived_size;
	uint32_t received_size;
	uint8_t is_completed;

	http_conn *parent_session;
} message;

typedef struct packet {
	// size http headers claim for packet
	size_t claimed_size;

	// size of filled content
	size_t filled_size;

	// max capacity of buffer
	// can be exapanded
	size_t buffer_capacity;
	uint8_t *buffer;

	message *parent_message;
} packet;

http_sock *open_http();
void close_http(http_sock *);
uint32_t get_port_number(http_sock *);

http_conn *accept_conn(http_sock *);
void close_conn(http_conn *);

message *get_message(http_conn *);
void free_message(message *);

packet *get_packet(message *);
void free_packet(packet *);
