#ifndef MH_SERVER_CONSOLE_OUTPUT_H
#define MH_SERVER_CONSOLE_OUTPUT_H

#include <stdio.h>

/* Console timestamps use local wall time, independent of simulation clocks. */
/* Like puts, including a timestamp for each line in multiline text. */
void	mh_server_console_puts(FILE *stream, const char *text);

/* One formatted line; the format includes its terminating newline. */
void	mh_server_console_printf(FILE *stream, const char *format, ...);

#endif
