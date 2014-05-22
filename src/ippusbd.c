#include <stdio.h>

#include "http/http.h"

int main(int argc, char *argv[])
{
	http_sock *sock = open_http();
	if (sock == NULL)
		goto cleanup;

	// TODO: print port then fork

	while (1) {
		http_conn *conn = accept_conn(sock);
		if (conn == NULL) {
			ERR("Opening connection failed");
			goto conn_error;
		}

		message *msg = get_message(conn);
		if (msg == NULL) {
			ERR("Generating message failed");
			goto conn_error;
		}

		packet *pkt = get_packet(msg);
		if (pkt == NULL) {
			ERR("Receiving packet failed");
			goto conn_error;
		}

		printf("%.*s", (int)pkt->size, pkt->buffer);

		puts("Hello world");
	conn_error:
		if (conn != NULL)
			free(conn);
		if (msg != NULL)
			free(msg);
	}

cleanup:
	if (sock != NULL)
		close_http(sock);

	return 1;
}
