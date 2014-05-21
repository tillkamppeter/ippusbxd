#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "http.h"


http_handle *open_http()
{
	http_handle *this = calloc(1, sizeof *this);
	if (this == NULL) {
		ERR("callocing this failed");
		goto error;
	}

	// Open [S]ocket [D]escriptor
	this->sd = -1;
	this->sd = socket(AF_INET6, SOCK_STREAM, 0);
	if (this->sd < 0) {
		ERR("sockect open failed");
		goto error;
	}

	// Configure socket params
	struct sockaddr_in6 addr;
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(0); // let kernel decide
	addr.sin6_addr = in6addr_any;

	// Bind to localhost
	if (bind(this->sd,
	        (struct sockaddr *)&addr,
	        sizeof addr) < 0) {
		ERR("bind failed");
		goto error;
	}


	// Let kernel over-accept HTTPMAXPENDCONNS number of connections
	if (listen(this->sd, HTTPMAXPENDINGCONNS) < 0) {
		ERR("listen failed on socket");
		goto error;
	}

	return this;

error:
	if (this->sd != -1)
		close(this->sd);
	if (this != NULL)
		free(this);

	return NULL;
}

packet *reveive_packet(http_conn *conn)
{
}

http_conn *accept_conn(http_sock *this)
{
	http_conn *conn = calloc(1, sizeof *msg);



	return conn;
}

void close_http(http_handle *this)
{
	close(this->sd);
	free(this);
}

/* Valid methods for determining http request
 * size are defined by W3 in RFC2616 section 4.4
 * link: http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
 */
