#include <stdio.h>
#include <stdarg.h>

#include <syslog.h>

#include "logging.h"

enum log_target setting_log_target;

void BASE_LOG(const char *fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);
	if (setting_log_target == LOGGING_STDERR)
		vfprintf(stderr, fmt, arg);
	else if (setting_log_target == LOGGING_SYSLOG)
		syslog(LOG_ERR, fmt, arg);
	va_end(arg);
}
