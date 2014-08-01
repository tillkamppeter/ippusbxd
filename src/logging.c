#include <stdio.h>
#include <stdarg.h>

#include <syslog.h>

#include "logging.h"
#include "options.h"

void BASE_LOG(enum log_level level, const char *fmt, ...)
{
	if (!g_options.verbose_mode && level != LOGGING_ERROR)
		return;

	va_list arg;
	va_start(arg, fmt);
	if (g_options.log_destination == LOGGING_STDERR)
		vfprintf(stderr, fmt, arg);
	else if (g_options.log_destination == LOGGING_SYSLOG)
		syslog(LOG_ERR, fmt, arg);
	va_end(arg);
}
