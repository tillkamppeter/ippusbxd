/* Copyright (C) 2014 Daniel Dressler and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#pragma once
#include <stdint.h>

enum log_target {
	LOGGING_STDERR,
	LOGGING_SYSLOG
};

struct options {
	// Runtime configuration
	uint16_t desired_port;
	int only_desired_port;
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
