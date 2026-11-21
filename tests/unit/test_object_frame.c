#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/contact_schedule.h"
#include "engine/simulation/object.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "object frame line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct object_fixture {
	unsigned int	index, pattern, contacts, attempts, residuals;
	unsigned int	*events, *event_count;
	int		fail;
};

static enum mh_error_code
visit_contact(void *opaque, enum mh_contact_stage stage, unsigned int pass,
	int *hit, struct mh_error *error_out)
{
	struct object_fixture *fixture;

	fixture = (struct object_fixture *)opaque;

	if (fixture->events != NULL)
		fixture->events[(*fixture->event_count)++] =
			fixture->index * 16 + (unsigned int)stage * 4 + pass +
			1;

	if (stage == MH_CONTACT_TRACK) {
		fixture->attempts++;
		*hit = (fixture->pattern >> pass) & 1U;

		if (*hit)
			fixture->contacts |= 1U
				<< ((fixture->index + pass) % 8);
	} else
		fixture->residuals++;

	return fixture->fail && stage == MH_CONTACT_RESIDUAL
		? mh_error_set(error_out, MH_ERROR_FAILED,
			"fixture residual failure")
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(void)
{
	struct object_fixture fixture;
	struct mh_object_state state, saved;
	int hit;

	memset(&fixture, 0, sizeof(fixture));
	hit = 1;
	CHECK(mh_object_contact_schedule(
		0, &hit, visit_contact, &fixture, NULL) == MH_ERROR_NONE);
	CHECK(hit && fixture.attempts == 3 && fixture.residuals == 1);
	memset(&fixture, 0, sizeof(fixture));
	CHECK(mh_object_contact_schedule(
		1, &hit, visit_contact, &fixture, NULL) == MH_ERROR_NONE);
	CHECK(hit && fixture.attempts == 0 && fixture.residuals == 1);
	memset(&fixture, 0, sizeof(fixture));
	fixture.pattern = 1;
	fixture.fail = 1;
	hit = 0;
	CHECK(mh_object_contact_schedule(0, &hit, visit_contact, &fixture,
		NULL) != MH_ERROR_NONE &&
		!hit);
	CHECK(fixture.attempts == 3 && fixture.residuals == 1);
	memset(&fixture, 0, sizeof(fixture));
	fixture.pattern = 6;
	CHECK(mh_object_contact_schedule(
		0, &hit, visit_contact, &fixture, NULL) == MH_ERROR_NONE);
	CHECK(!hit && fixture.attempts == 1 && fixture.residuals == 0);
	memset(&state, 0, sizeof(state));
	state.frame_impact = 1;
	state.flags = 2;
	state.sleep_time = 3;
	state.contacts[7].hit = 1;
	saved = state;
	mh_object_begin_frame(&state);
	saved.frame_impact = 0;
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(mh_object_contact_schedule(
		2, &hit, visit_contact, &fixture, NULL) != MH_ERROR_NONE);
	puts("scenery frame latch, sleeping residuals, pass order and reset passed");

	return 0;
}
