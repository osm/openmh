#include "server/logging/stream.h"

FILE *
mh_server_log_stream(const char *path)
{
	return fopen(path, "ab");
}
