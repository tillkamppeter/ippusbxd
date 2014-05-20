#include <stdio.h>

#include "http/http.h"

int main(int argc, char *argv[])
{
	http_handle *httph = open_http();
	if (httph == NULL)
		goto cleanup;

	puts("Hello world");

cleanup:
	if (httph != NULL)
		close_http(httph);

	return 1;
}
