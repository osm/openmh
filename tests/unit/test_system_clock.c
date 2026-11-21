#ifdef MH_TEST_CLOCK_WRAP
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#endif

#include <stdio.h>
#include <time.h>

#include "backends/clock/system_provider.h"
#include "engine/clock/paused.h"
#include "engine/core/limits.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "system clock:%d: %s\n", __LINE__,     \
				#c);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

#ifdef MH_TEST_CLOCK_WRAP
static struct timespec sample_time, slept;
static unsigned int samples, sleeps;
static int fail_sample, interrupt_sleep, invalid_sleep;
static clockid_t sampled_clock;

int
__wrap_clock_gettime(clockid_t clock, struct timespec *output)
{
	samples++;
	sampled_clock = clock;

	if (fail_sample) {
		errno = EIO;
		return -1;
	}

	*output = sample_time;

	return 0;
}

int
__wrap_nanosleep(const struct timespec *request, struct timespec *remaining)
{
	sleeps++;
	slept = *request;

	if (interrupt_sleep) {
		interrupt_sleep = 0;
		remaining->tv_sec = 0;
		remaining->tv_nsec = 23000000;
		errno = EINTR;
		return -1;
	}

	if (invalid_sleep) {
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static int
check_system_clock(const struct mh_clock_source *source)
{
	struct mh_clock_counter value;
	struct mh_clock_date date;

	CHECK(samples == 0);
	CHECK(mh_clock_source_date(source, &date, NULL) == MH_ERROR_NONE);
	CHECK(samples == 0);
	sample_time.tv_sec = 4;
	sample_time.tv_nsec = 294967295;
	value = mh_clock_source_counter(source);
	CHECK(value.low == MH_U32_MAX && value.high == 0);
#ifdef CLOCK_MONOTONIC_RAW
	CHECK(sampled_clock == CLOCK_MONOTONIC_RAW);
#else
	CHECK(sampled_clock == CLOCK_MONOTONIC);
#endif
	sample_time.tv_nsec++;
	value = mh_clock_source_counter(source);
	CHECK(value.low == 0 && value.high == 1);
	sample_time.tv_sec = 123456789;
	sample_time.tv_nsec = 987654321;
	value = mh_clock_source_counter(source);
	CHECK(value.high == 0x01b69b4bUL && value.low == 0xe052fab1UL);
	sample_time.tv_sec = 2147483647;
	sample_time.tv_nsec = 999999999;
	value = mh_clock_source_counter(source);
	CHECK(value.high == 499999999UL && value.low == MH_U32_MAX);
	fail_sample = 1;
	value = mh_clock_source_counter(source);
	CHECK(value.high == 499999999UL && value.low == MH_U32_MAX);
	fail_sample = 0;

	sample_time.tv_sec = 3;
	sample_time.tv_nsec = 999500000;
	CHECK(mh_clock_source_ticks(source) == 0);
	sample_time.tv_sec = 4;
	sample_time.tv_nsec = 500000;
	CHECK(mh_clock_source_ticks(source) == 1);
	sample_time.tv_sec = 4294971;
	sample_time.tv_nsec = 294500000;
	CHECK(mh_clock_source_ticks(source) == MH_U32_MAX);
	sample_time.tv_nsec += 1000000;
	CHECK(mh_clock_source_ticks(source) == 0);
	CHECK(mh_system_clock_source() == source);
	sample_time.tv_nsec += 1000000;
	CHECK(mh_clock_source_ticks(source) == 1);

	CHECK(mh_clock_source_delay(source, 1234, NULL) == MH_ERROR_NONE);
	CHECK(sleeps == 1 && slept.tv_sec == 1 && slept.tv_nsec == 234000000);
	interrupt_sleep = 1;
	CHECK(mh_clock_source_delay(source, 100, NULL) == MH_ERROR_NONE);
	CHECK(sleeps == 3 && slept.tv_sec == 0 && slept.tv_nsec == 23000000);
	invalid_sleep = 1;
	CHECK(mh_clock_source_delay(source, 1, NULL) == MH_ERROR_NONE);
	CHECK(sleeps == 4);

	return 0;
}
#else
static int
check_system_clock(const struct mh_clock_source *source)
{
	struct mh_clock_counter before, after, rate;
	struct mh_paused_clock paused;
	long double elapsed;
	unsigned long ticks;

	before = mh_clock_source_counter(source);
	rate = mh_clock_source_frequency(source);
	ticks = mh_clock_source_ticks(source);
	CHECK(mh_clock_source_delay(source, 20, NULL) == MH_ERROR_NONE);
	after = mh_clock_source_counter(source);
	CHECK(mh_clock_counter_elapsed(&before, &after, &rate, 1, &elapsed,
		NULL) == MH_ERROR_NONE);
	CHECK(elapsed >= .019L);
	CHECK(((mh_clock_source_ticks(source) - ticks) & MH_U32_MAX) >= 19);
	CHECK(mh_paused_clock_init(&paused, source, NULL) == MH_ERROR_NONE);
	mh_paused_clock_set(&paused, 1);
	before = mh_clock_source_counter(&paused.source);
	CHECK(mh_clock_source_delay(source, 5, NULL) == MH_ERROR_NONE);
	after = mh_clock_source_counter(&paused.source);
	CHECK(before.low == after.low && before.high == after.high);
	mh_paused_clock_set(&paused, 0);
	CHECK(mh_clock_source_delay(source, 5, NULL) == MH_ERROR_NONE);
	after = mh_clock_source_counter(&paused.source);
	CHECK(mh_clock_counter_elapsed(&before, &after, &rate, 1, &elapsed,
		NULL) == MH_ERROR_NONE &&
		elapsed >= .004L);

	return 0;
}
#endif

int
main(void)
{
	const struct mh_clock_source *source;
	struct mh_clock_counter rate;
	struct mh_clock_date date;

	source = mh_system_clock_source();
	rate = mh_clock_source_frequency(source);
	CHECK(rate.high < MH_U32_SIGN_BIT && (rate.low != 0 || rate.high != 0));
#ifdef MH_TEST_CLOCK_WRAP
	CHECK(rate.low == 1000000000UL && rate.high == 0);
#endif
	CHECK(mh_clock_source_can_delay(source));
	CHECK(check_system_clock(source) == 0);
	CHECK(mh_clock_source_date(source, &date, NULL) == MH_ERROR_NONE);
	CHECK(date.year >= 1900 && date.month >= 1 && date.month <= 12 &&
		date.day >= 1 && date.day <= 31);
	CHECK(mh_clock_source_date(mh_system_clock_source(), NULL, NULL) ==
		MH_ERROR_INVALID);
	puts("System clock: counters, ticks, pacing and calendar passed");

	return 0;
}
