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
} tcp_sock_t;

typedef struct {
	int sd;
} tcp_conn_t;

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

	tcp_conn_t *parent_session;
} http_message_t;

typedef struct packet {
	// size http headers claim for packet
	size_t claimed_size;

	// size of filled content
	size_t filled_size;

	// max capacity of buffer
	// can be exapanded
	size_t buffer_capacity;
	uint8_t *buffer;

	http_message_t *parent_message;
} http_packet_t;

tcp_sock_t *tcp_open(uint32_t);
void tcp_close(tcp_sock_t *);
uint32_t get_port_number(tcp_sock_t *);

tcp_conn_t *tcp_conn_accept(tcp_sock_t *);
void tcp_conn_close(tcp_conn_t *);

http_message_t *tcp_message_get(tcp_conn_t *);
void free_message(http_message_t *);

http_packet_t *get_packet(http_message_t *);
void free_packet(http_packet_t *);
