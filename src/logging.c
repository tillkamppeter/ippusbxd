#include <stdio.h>
#include <stdarg.h>

#include <syslog.h>

#include "logging.h"
#include "options.h"

void BASE_LOG(const char *fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);
	if (g_options.log_destination == LOGGING_STDERR)
		vfprintf(stderr, fmt, arg);
	else if (g_options.log_destination == LOGGING_SYSLOG)
		syslog(LOG_ERR, fmt, arg);
	va_end(arg);
}
