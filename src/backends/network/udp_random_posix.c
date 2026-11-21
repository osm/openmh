#include <stdio.h>

#include "backends/network/udp_challenge.h"

int
mh_udp_random(void *output, size_t size)
{
	FILE *stream;
	size_t count;

	stream = fopen("/dev/urandom", "rb");

	if (stream == NULL)
		return 0;

	count = fread(output, 1, size, stream);
	fclose(stream);

	return count == size;
}
