#ifndef MH_SERVER_LOG_STREAM_H
#define MH_SERVER_LOG_STREAM_H

#include <stdio.h>

/* Owned binary append stream; path is UTF-8 on Windows. */
FILE	*mh_server_log_stream(const char *path);

#endif
