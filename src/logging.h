#pragma once

enum log_target {
	LOGGING_STDERR,
	LOGGING_SYSLOG
};

extern enum log_target setting_log_target;

void BASE_LOG(const char *, ...);

#define ERR(x, ...) BASE_LOG("Error: " x "\n", #__VA_ARGS__)
#define WARN(x, ...) BASE_LOG("Warning: " x "\n", #__VA_ARGS__)
#define CONF(x, ...) BASE_LOG("Standard Conformance Failure: " x "\n", #__VA_ARGS__)
