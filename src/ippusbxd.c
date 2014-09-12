#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <unistd.h>
#include <getopt.h>
#include <pthread.h>

#include "options.h"
#include "logging.h"
#include "http.h"
#include "tcp.h"
#include "usb.h"

struct service_thread_param {
	struct tcp_conn_t *tcp;
	struct usb_sock_t *usb_sock;
	pthread_t thread_handle;
};
static void *service_connection(void *arg_void)
{
	struct service_thread_param *arg =
		(struct service_thread_param *)arg_void;

	// clasify priority
	while (!arg->tcp->is_closed) {
		struct usb_conn_t *usb = NULL;
		struct http_message_t *server_msg = NULL;
		struct http_message_t *client_msg = NULL;

		// Client's request
		NOTE("Client msg starting");
		client_msg = http_message_new();
		if (client_msg == NULL) {
			ERR("Failed to create message");
			break;
		}

		while (!client_msg->is_completed) {
			struct http_packet_t *pkt;
			pkt = tcp_packet_get(arg->tcp, client_msg);
			if (pkt == NULL) {
				if (arg->tcp->is_closed) {
					NOTE("Client closed connection\n");
					goto cleanup_subconn;
				}
				ERR_AND_EXIT("Got null packet from tcp");
			}
			if (usb == NULL) {
				usb = usb_conn_aquire(arg->usb_sock, 1);
				if (usb == NULL) {
					ERR("Failed to aquire usb interface");
					packet_free(pkt);
					goto cleanup_subconn;
				}
				NOTE("Interface #%d: aquired usb conn",
						usb->interface_index);
			}

			NOTE("Pkt from tcp\n===\n%.*s\n===", (int)pkt->filled_size, pkt->buffer);
			usb_conn_packet_send(usb, pkt);
			packet_free(pkt);
		}
		message_free(client_msg);
		client_msg = NULL;
		NOTE("Interface #%d: Client msg completed\n",
				usb->interface_index);


		// Server's response
		NOTE("Interface #%d: Server msg starting",
				usb->interface_index);
		server_msg = http_message_new();
		if (server_msg == NULL) {
			ERR("Failed to create message");
			goto cleanup_subconn;
		}
		while (!server_msg->is_completed) {
			struct http_packet_t *pkt;
			pkt = usb_conn_packet_get(usb, server_msg);
			if (pkt == NULL)
				break;

			NOTE("Pkt from usb\n===\n%.*s\n===",
					(int)pkt->filled_size, pkt->buffer);
			tcp_packet_send(arg->tcp, pkt);
			packet_free(pkt);
			NOTE("Interface #%d: Server pkt done",
					usb->interface_index);
		}
		NOTE("Interface #%d: Server msg completed\n",
				usb->interface_index);

cleanup_subconn:
		if (client_msg != NULL)
			message_free(client_msg);
		if (server_msg != NULL)
			message_free(server_msg);
		if (usb != NULL)
			usb_conn_release(usb);
	}



	tcp_conn_close(arg->tcp);
	free(arg);
	return NULL;
}

static void start_daemon()
{
	// Capture USB device
	struct usb_sock_t *usb_sock = usb_open();
	if (usb_sock == NULL)
		goto cleanup_usb;

	// Capture a socket
	uint16_t desired_port = g_options.desired_port;
	struct tcp_sock_t *tcp_socket = tcp_open(desired_port);
	if (tcp_socket == NULL)
		goto cleanup_tcp;

	uint16_t real_port = tcp_port_number_get(tcp_socket);
	if (desired_port != 0 && desired_port != real_port) {
		ERR("Received port number did not match requested port number."
		    " The requested port number may be too high.");
		goto cleanup_tcp;
	}
	printf("%u|", real_port);
	fflush(stdout);

	// Lose connection to caller
	if (!g_options.nofork_mode && fork() > 0)
		exit(0);


	// Register for unplug event
	if (usb_can_callback(usb_sock))
		usb_register_callback(usb_sock);

	for (;;) {
		struct service_thread_param *args = calloc(1, sizeof(*args));
		if (args == NULL) {
			ERR("Failed to alloc space for thread args");
			goto cleanup_thread;
		}

		args->usb_sock = usb_sock;
		args->tcp = tcp_conn_accept(tcp_socket);
		if (args->tcp == NULL) {
			ERR("Failed to open tcp connection");
			goto cleanup_thread;
		}

		int status = pthread_create(&args->thread_handle, NULL,
		                            &service_connection, args);
		if (status) {
			ERR("Failed to spawn thread, error %d", status);
			goto cleanup_thread;
		}

		continue;

	cleanup_thread:
		if (args != NULL) {
			if (args->tcp != NULL)
				tcp_conn_close(args->tcp);
			free(args);
		}
		break;
	}

cleanup_tcp:
	if (tcp_socket!= NULL)
		tcp_close(tcp_socket);
cleanup_usb:
	if (usb_sock != NULL)
		usb_close(usb_sock);
	return;
}

static uint16_t strto16(const char *str)
{
	unsigned long val = strtoul(str, NULL, 16);
	if (val > UINT16_MAX)
		exit(1);
	return (uint16_t)val;
}

int main(int argc, char *argv[])
{
	int c;
	g_options.log_destination = LOGGING_STDERR;

	while ((c = getopt(argc, argv, "qnhdp:s:lv:m:")) != -1) {
		switch (c) {
		case '?':
		case 'h':
			g_options.help_mode = 1;
			break;
		case 'p':
		{
			long long port = 0;
			// Request specific port
			port = atoi(optarg);
			if (port < 0) {
				ERR("Port number must be non-negative");
				return 1;
			}
			if (port > UINT16_MAX) {
				ERR("Port number must be %u or less, "
				    "but not negative", UINT16_MAX);
				return 2;
			}
			g_options.desired_port = (uint16_t)port;
			break;
		}
		case 'l':
			g_options.log_destination = LOGGING_SYSLOG;
			break;
		case 'd':
			g_options.nofork_mode = 1;
			g_options.verbose_mode = 1;
			break;
		case 'q':
			g_options.verbose_mode = 1;
			break;
		case 'n':
			g_options.nofork_mode = 1;
			break;
		case 'v':
			g_options.vendor_id = strto16(optarg);
			break;
		case 'm':
			g_options.product_id = strto16(optarg);
			break;
		case 's':
			g_options.serial_num = (unsigned char *)optarg;
			break;
		}
	}

	if (g_options.help_mode) {
		printf(
		"Usage: %s -v <vendorid> -m <productid> -p <port>\n"
		"Options:\n"
		"  -h           Show this help message\n"
		"  -v <vid>     Vendor ID of desired printer\n"
		"  -m <pid>     Product ID of desired printer\n"
		"  -s <serial>  Serial number of desired printer\n"
		"  -p <portnum> Port number to bind against\n"
		"  -l           Redirect logging to syslog\n"
		"  -q           Enable verbose tracing\n"
		"  -d           Debug mode for verbose output and no fork\n"
		"  -n           No fork mode\n"
		, argv[0]);
		return 0;
	}

	start_daemon();
	return 0;
}
