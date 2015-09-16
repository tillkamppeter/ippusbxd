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
#include <stdlib.h>
#include <string.h>

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

char* hexdump (void *addr, int len) {
  int i;
  char linebuf[17];
  char *pc = (char*)addr;
  char *outbuf, *outbufp;

  outbuf = calloc((len / 16 + 1) * 80, sizeof(char));
  if (outbuf == NULL)
    return "*** Failed to allocate memory for hex dump! ***";
  outbufp = outbuf;

  // Process every byte in the data.
  for (i = 0; i < len; i++) {
    if ((i % 16) == 0) { // Multiple of 16 means new line (with line offset).
      if (i != 0) { // Just don't print ASCII for the zeroth line.
	sprintf (outbufp, "  %s\n", linebuf);
	outbufp += strlen(linebuf) + 3;
      }
      // Output the offset.
      sprintf (outbufp, "  %08x ", i);
      outbufp += 11;
    }

    // Now the hex code for the specific character.
    sprintf (outbufp, " %02x", pc[i]);
    outbufp += 3;

    // And store a printable ASCII character for later.
    if ((pc[i] < 0x20) || (pc[i] > 0x7e))
      linebuf[i % 16] = '.';
    else
      linebuf[i % 16] = pc[i];
    linebuf[(i % 16) + 1] = '\0';
  }

  // Pad out last line if not exactly 16 characters.
  while ((i % 16) != 0) {
    sprintf (outbufp, "   ");
    outbufp += 3;
    i++;
  }

  // And print the final ASCII bit.
  sprintf (outbufp, "  %s\n", linebuf);

  return outbuf;
}
