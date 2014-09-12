#pragma once
#include <stdint.h>

enum log_target {
	LOGGING_STDERR,
	LOGGING_SYSLOG
};

struct options {
	// Runtime configuration
	uint16_t desired_port;
	enum log_target log_destination;

	// Behavior
	int help_mode;
	int verbose_mode;
	int nofork_mode;

	// Printer indentity
	unsigned char *serial_num;
	int vendor_id;
	int product_id;
};

extern struct options g_options;
