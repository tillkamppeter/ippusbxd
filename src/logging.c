#include <stdio.h>
#include <stdarg.h>

#include "logging.h"

enum log_target setting_log_target;

void BASE_LOG(const char *fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);
	vfprintf(stderr, fmt, arg);
	va_end(arg);
}
