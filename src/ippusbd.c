#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include <unistd.h>
#include <getopt.h>

#include "http.h"
#include "usb.h"

void start_daemon(uint32_t requested_port)
{
	// Capture USB device
	usb_sock_t *usb = open_usb();
	if (usb == NULL)
		goto cleanup_usb;

	// Capture a socket
	tcp_sock_t *sock = tcp_open(requested_port);
	if (sock == NULL)
		goto cleanup_http;

	// TODO: print port then fork
	uint32_t real_port = get_port_number(sock);
	if (requested_port != 0 && requested_port != real_port) {
		ERR("Received port number did not match requested port number. "
		    "The requested port number may be too high.");
		goto cleanup_http;
	}
	printf("%u\n", real_port);

	while (1) {
		tcp_conn_t *conn = tcp_conn_accept(sock);
		if (conn == NULL) {
			ERR("Opening connection failed");
			goto conn_cleanup;
		}

		// TODO: spawn thread

		http_message_t *msg = tcp_message_get(conn);
		if (msg == NULL) {
			ERR("Generating message failed");
			goto conn_cleanup;
		}

		while (!msg->is_completed) {

			http_packet_t *pkt = get_packet(msg);
			if (pkt == NULL) {
				ERR("Receiving packet failed");
				goto conn_cleanup;
			}

			printf("%.*s", (int)pkt->filled_size, pkt->buffer);

			send_packet_usb(usb, pkt);

			free_packet(pkt);
		http_packet_t *usb_pkt = get_packet_usb(usb);
		printf("%.*s", (int)usb_pkt->filled_size, usb_pkt->buffer);
		}



	conn_cleanup:
		if (msg != NULL)
			free_message(msg);
		if (conn != NULL)
			tcp_conn_close(conn);
	}

cleanup_http:
	if (sock != NULL)
		tcp_close(sock);
cleanup_usb:
	if (usb != NULL)
		close_usb(usb);
	return;
}

int main(int argc, char *argv[])
{
	int c;
	long long port = 0;
	int show_help = 0;
	while ((c = getopt(argc, argv, "hp:u:o:")) != -1) {
		switch (c) {
		case '?':
		case 'h':
			show_help = 1;
			break;
		case 'p':
			// Request specific port
			port = atoi(optarg);
			if (port < 0) {
				ERR("Port number must be non-negative");
				return 1;
			}
			if (port > (long long)UINT_MAX) {
				ERR("Port number must be %u or less, "
				    "but not negative", UINT_MAX);
				return 2;
			}
			break;
		case 'u':
			// [u]sb device to bind with
			break;
		case 'o':
			// Error log file
			break;
		}
	}

	if (show_help) {
		printf(
		"Usage: %s -v <vendorid> -m <productid> -p <port>\n"
		"Options:\n"
		"  -h           Show this help message\n"
		"  -v <vid>     Vendor ID of desired printer\n"
		"  -m <pid>     Product ID of desired printer\n"
		"  -s <serial>  Serial number of desired printer\n"
		"  -p <portnum> Port number to bind against\n"
		"  -l           Send errors to syslog\n"
		, argv[0]);
		return 0;
	}

	start_daemon((uint32_t)port);
	return 0;
}
