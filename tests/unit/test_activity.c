#include <stdio.h>
#include <string.h>

#include "client/application/activity.h"
#include "engine/core/error.h"
#include "interfaces/clock/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "activity line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct activity_fixture {
	unsigned int	steps, closes;
	int		fail;
	char		message[600];
};

static char sequence[32];
static unsigned int events;

struct pacing_fixture {
	unsigned long	waited;
	unsigned int	samples;
};

static struct mh_clock_counter
read_counter(void *context)
{
	struct pacing_fixture *pacing;
	struct mh_clock_counter value;

	pacing = context;
	pacing->samples++;
	value.low = 1000;
	value.high = 0;

	return value;
}

static unsigned long
read_ticks(void *context)
{
	struct pacing_fixture *pacing;

	pacing = context;
	pacing->samples++;

	return 0;
}

static void
delay(void *context, unsigned long milliseconds)
{
	struct pacing_fixture *pacing;

	pacing = context;
	sequence[events++] = 'D';
	pacing->waited += milliseconds;
}

static enum mh_app_status
step_activity(void *context, unsigned long *delay, struct mh_error *error)
{
	struct activity_fixture *fixture;

	fixture = (struct activity_fixture *)context;
	sequence[events++] = 'S';
	fixture->steps++;

	if (fixture->fail) {
		mh_error_set(error, MH_ERROR_IO, fixture->message);
		return MH_APP_FAILED;
	}

	if (fixture->steps == 3)
		return MH_APP_DONE;

	*delay = 7;

	return MH_APP_RUNNING;
}

static void
close_activity(void *context)
{
	struct activity_fixture *fixture;

	fixture = (struct activity_fixture *)context;
	sequence[events++] = 'C';
	fixture->closes++;
	strcpy(fixture->message, "released storage");
}

int
main(void)
{
	static const struct mh_activity_ops ops = {
		step_activity, close_activity};

	static const struct mh_clock_ops clock_ops = {
		read_counter, read_counter, read_ticks, delay, NULL};

	static const struct mh_clock_ops host_ops = {
		read_counter, read_counter, read_ticks, NULL, NULL};

	struct mh_clock_source clock, host_clock;
	struct pacing_fixture pacing, host_pacing;
	struct mh_activity activity;
	struct activity_fixture fixture;
	struct mh_error error, diagnostic;
	enum mh_error_code status;

	memset(&pacing, 0, sizeof(pacing));
	memset(&host_pacing, 0, sizeof(host_pacing));
	CHECK(mh_clock_source_init(&clock, &clock_ops, &pacing, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clock_source_init(&host_clock, &host_ops, &host_pacing,
		NULL) == MH_ERROR_NONE);
	memset(&fixture, 0, sizeof(fixture));
	mh_activity_init(&activity);
	CHECK(!mh_activity_is_open(&activity));
	CHECK(mh_activity_start(&activity, &fixture, &ops, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_activity_start(&activity, &fixture, &ops, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	CHECK(fixture.steps == 1 && mh_activity_delay(&activity) == 7);
	CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	CHECK(mh_activity_step(&activity) == MH_APP_DONE);
	CHECK(mh_activity_step(&activity) == MH_APP_DONE);
	CHECK(fixture.steps == 3 && mh_activity_delay(&activity) == 0);
	mh_activity_close(&activity);
	mh_activity_close(&activity);
	CHECK(fixture.closes == 1 && !mh_activity_is_open(&activity));

	memset(&fixture, 0, sizeof(fixture));
	memset(sequence, 0, sizeof(sequence));
	events = 0;
	CHECK(mh_activity_start(&activity, &fixture, &ops, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_activity_run_with_clock(&activity, &clock, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(sequence, "SDSDSC") == 0);
	CHECK(pacing.waited == 14 && fixture.steps == 3 && fixture.closes == 1);
	CHECK(!mh_activity_is_open(&activity));

	memset(&fixture, 0, sizeof(fixture));
	fixture.fail = 1;
	strcpy(fixture.message, "failed before cleanup");
	CHECK(mh_activity_start(&activity, &fixture, &ops, NULL) ==
		MH_ERROR_NONE);
	status = mh_activity_run_with_clock(&activity, &clock, &error);
	CHECK(status == MH_ERROR_IO &&
		strcmp(error.message, "failed before cleanup") == 0);
	CHECK(mh_activity_error(&activity, NULL) == MH_ERROR_IO);
	CHECK(strcmp(fixture.message, "released storage") == 0);
	mh_activity_error(&activity, &diagnostic);
	CHECK(strcmp(diagnostic.message, error.message) == 0);
	CHECK(mh_activity_step(&activity) == MH_APP_FAILED);
	CHECK(fixture.steps == 1 && fixture.closes == 1);

	memset(fixture.message, 'x', sizeof(fixture.message) - 1);
	fixture.message[sizeof(fixture.message) - 1] = 0;
	CHECK(mh_activity_start(&activity, &fixture, &ops, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_activity_step(&activity) == MH_APP_FAILED);
	mh_activity_error(&activity, &diagnostic);
	CHECK(strlen(diagnostic.message) == MH_ACTIVITY_ERROR_SIZE - 1);
	mh_activity_close(&activity);
	mh_activity_error(&activity, &diagnostic);
	CHECK(strlen(diagnostic.message) == MH_ACTIVITY_ERROR_SIZE - 1);

	memset(&fixture, 0, sizeof(fixture));
	CHECK(mh_activity_start(&activity, &fixture, &ops, NULL) ==
		MH_ERROR_NONE);
	mh_activity_close(&activity);
	CHECK(fixture.steps == 0 && fixture.closes == 1);
	CHECK(mh_activity_step(&activity) == MH_APP_DONE);
	CHECK(fixture.steps == 0 && fixture.closes == 1);

	/* A host without native waits can still drive the same activity. */
	memset(&fixture, 0, sizeof(fixture));
	CHECK(mh_activity_start(&activity, &fixture, &ops, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_activity_run_with_clock(&activity, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_activity_run_with_clock(&activity, &host_clock, &error) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_activity_is_open(&activity));
	CHECK(fixture.steps == 0 && fixture.closes == 0);
	CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	CHECK(mh_activity_delay(&activity) == 7);
	CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	CHECK(mh_activity_step(&activity) == MH_APP_DONE);
	mh_activity_close(&activity);
	CHECK(fixture.steps == 3 && fixture.closes == 1);
	CHECK(pacing.waited == 14 && pacing.samples == 0);
	CHECK(host_pacing.waited == 0 && host_pacing.samples == 0);
	puts("Activity ownership, pacing, terminal states, cancellation and retained errors passed");

	return 0;
}
