#pragma once

enum log_target {
	LOGGING_STDERR,
	LOGGING_SYSLOG
};

extern enum log_target setting_log_target;

void BASE_LOG(const char *, ...);

#define ERR(x, ...) BASE_LOG("Error: " x "\n", #__VA_ARGS__)
