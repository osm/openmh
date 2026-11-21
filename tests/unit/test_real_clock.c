#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/frame_time.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "real clock line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_clock_counter counter, frequency;
	unsigned long word;
	long double milliseconds;

	CHECK(mh_clock_word(0, &word, NULL) == MH_ERROR_NONE && word == 0);
	CHECK(mh_clock_word(4294967295.75L, &word, NULL) == MH_ERROR_NONE &&
		word == MH_U32_MAX);
	CHECK(mh_clock_word(4294967296.75L, &word, NULL) == MH_ERROR_NONE &&
		word == 0);
	CHECK(mh_clock_word(8589934593.75L, &word, NULL) == MH_ERROR_NONE &&
		word == 1);
	CHECK(mh_clock_word(9223372036854773760.0L, &word, NULL) ==
			MH_ERROR_NONE &&
		word == 4294965248UL);
#if LDBL_MANT_DIG >= 64
	CHECK(mh_clock_word(9223372036854775807.5L, &word, NULL) ==
			MH_ERROR_NONE &&
		word == MH_U32_MAX);
	CHECK(mh_clock_word(MH_U32_MODULUS - 0.0000000004656612873077392578125L,
		&word, NULL) == MH_ERROR_NONE &&
		word == MH_U32_MAX);
#endif
	word = 123;
	CHECK(mh_clock_word(-1, &word, NULL) != MH_ERROR_NONE && word == 123);
	CHECK(mh_clock_word(9223372036854775808.0L, &word, NULL) !=
			MH_ERROR_NONE &&
		word == 123);
	CHECK(mh_clock_word(LDBL_MAX, &word, NULL) != MH_ERROR_NONE &&
		word == 123);
	CHECK(mh_clock_word(1, NULL, NULL) != MH_ERROR_NONE);
	counter.high = 1;
	counter.low = 1;
	frequency.high = 0;
	frequency.low = 1000;
	CHECK(mh_clock_counter_ms(&counter, &frequency, &milliseconds, NULL) ==
			MH_ERROR_NONE &&
		milliseconds == 4294967297.0L);
	frequency.low = 0;
	milliseconds = 123;
	CHECK(mh_clock_counter_ms(&counter, &frequency, &milliseconds, NULL) !=
			MH_ERROR_NONE &&
		milliseconds == 123);
	frequency.high = MH_U32_SIGN_BIT;
	CHECK(mh_clock_counter_ms(&counter, &frequency, &milliseconds, NULL) !=
			MH_ERROR_NONE &&
		milliseconds == 123);
	CHECK(mh_clock_counter_ms(NULL, &frequency, &milliseconds, NULL) !=
			MH_ERROR_NONE &&
		milliseconds == 123);
	CHECK(mh_clock_counter_ms(&counter, NULL, &milliseconds, NULL) !=
			MH_ERROR_NONE &&
		milliseconds == 123);
	CHECK(mh_clock_counter_ms(&counter, &frequency, NULL, NULL) !=
		MH_ERROR_NONE);
	frequency.high = 0;
	frequency.low = 1;
	counter.low = counter.high = MH_U32_MAX;
	CHECK(mh_clock_counter_ms(&counter, &frequency, &milliseconds, NULL) ==
			MH_ERROR_NONE &&
		milliseconds == -1000);
	puts("real clock: signed counter, extended milliseconds and low-word timestamps pass");

	return 0;
}
