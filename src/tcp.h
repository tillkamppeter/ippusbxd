#pragma once
#include <stdint.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "http.h"

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

tcp_sock_t *tcp_open(uint32_t);
void tcp_close(tcp_sock_t *);
uint32_t get_port_number(tcp_sock_t *);

tcp_conn_t *tcp_conn_accept(tcp_sock_t *);
void tcp_conn_close(tcp_conn_t *);

http_message_t *tcp_message_get(tcp_conn_t *);

http_packet_t *get_packet(tcp_conn_t *, http_message_t *);
