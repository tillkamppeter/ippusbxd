#include <stdio.h>
#include <stdlib.h>

#include "http/http.h"

int main(int argc, char *argv[])
{
	http_sock *sock = open_http();
	if (sock == NULL)
		goto cleanup;

	// TODO: print port then fork
	uint32_t port = get_port_number(sock);
	printf("%u\n", port);

	while (1) {
		http_conn *conn = accept_conn(sock);
		if (conn == NULL) {
			ERR("Opening connection failed");
			goto conn_cleanup;
		}

		// TODO: spawn thread

		message *msg = get_message(conn);
		if (msg == NULL) {
			ERR("Generating message failed");
			goto conn_cleanup;
		}

		while (!msg->is_completed) {

			packet *pkt = get_packet(msg);
			if (pkt == NULL) {
				ERR("Receiving packet failed");
				goto conn_cleanup;
			}

			printf("%.*s", (int)pkt->size, pkt->buffer);

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
