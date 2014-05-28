#include <stdio.h>
#include <stdlib.h>

#include "http/http.h"
#include "usb/usb.h"

int main(int argc, char *argv[])
{
	// Capture USB device
	usb_sock_t *usb = open_usb();

	// Capture a socket
	http_sock_t *sock = open_http();
	if (sock == NULL)
		goto cleanup;

	// TODO: print port then fork
	uint32_t port = get_port_number(sock);
	printf("%u\n", port);

	while (1) {
		http_conn_t *conn = accept_conn(sock);
		if (conn == NULL) {
			ERR("Opening connection failed");
			goto conn_cleanup;
		}

		// TODO: spawn thread

		http_message_t *msg = get_message(conn);
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

			free_packet(pkt);
		}

	conn_cleanup:
		if (msg != NULL)
			free_message(msg);
		if (conn != NULL)
			close_conn(conn);
	}

cleanup:
	if (sock != NULL)
		close_http(sock);

	return 0;
}
