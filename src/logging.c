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
