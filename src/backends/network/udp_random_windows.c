#include <limits.h>
#include <windows.h>

/* BCrypt declarations depend on the Windows base types. */
#include <bcrypt.h>

#include "backends/network/udp_challenge.h"

int
mh_udp_random(void *output, size_t size)
{
	return size <= ULONG_MAX &&
		BCryptGenRandom(NULL, output, (ULONG)size,
			BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
