#pragma once

enum log_target {
	LOGGING_STDERR,
	LOGGING_SYSLOG
};

struct options {
	// Runtime configuration
	unsigned int desired_port;
	enum log_target log_destination;

	// Behavior
	int help_mode;
	int verbose_mode;
	int nofork_mode;

	// Printer indentity
	char *serial_num;
	int vendor_id;
	int product_id;
};

extern struct options g_options;
