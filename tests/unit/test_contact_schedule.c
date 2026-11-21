#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/contact_schedule.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "contact schedule line %d: %s\n",      \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct contact_fixture {
	unsigned int	track[MH_RACE_MAX_CARS], pair[MH_RACE_MAX_CARS][3];
	unsigned int	current[MH_RACE_MAX_CARS], retained[MH_RACE_MAX_CARS];
	unsigned int	count, events[256], fail;
};

static enum mh_error_code
visit_contact(void *opaque, enum mh_contact_stage stage, unsigned int car,
	unsigned int peer, unsigned int pass, int *hit,
	struct mh_error *error_out)
{
	struct contact_fixture *fixture;
	unsigned int event;

	fixture = (struct contact_fixture *)opaque;

	if (fixture->fail && fixture->count == fixture->fail)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "injected contact failure");

	if (fixture->count >= 200)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "contact event overflow");

	event = (unsigned int)stage * 65536U + car * 256U + pass * 16U + peer;

	/* Observe which peer's retained turn is visible to this call. */
	if (stage == MH_CONTACT_PAIR)
		event |= fixture->retained[peer] << 20;

	fixture->events[++fixture->count] = event;

	if (stage == MH_CONTACT_TRACK) {
		*hit = (fixture->track[car] >> pass) & 1U;

		if (*hit)
			fixture->current[car] = 16U + car * 3U + pass;
	} else if (stage == MH_CONTACT_PAIR)
		*hit = (fixture->pair[car][pass] >> peer) & 1U;
	else
		fixture->retained[car] = fixture->current[car];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
prepare_contacts(struct contact_fixture *fixture)
{
	unsigned int i;

	memset(fixture, 0, sizeof(*fixture));

	for (i = 0; i < MH_RACE_MAX_CARS; i++)
		fixture->retained[i] = i + 1;
}

int
main(void)
{
	struct contact_fixture fixture;

	prepare_contacts(&fixture);
	CHECK(mh_contact_schedule(8, 7, 2, visit_contact, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture.count == 8); /* Local 0/2: track, two peers, residual. */
	CHECK((fixture.events[1] & 0xfffffU) == 0x10000U);
	CHECK((fixture.events[4] & 0xfffffU) == 0x30000U);
	CHECK((fixture.events[5] & 0xfffffU) == 0x10202U);
	CHECK(fixture.retained[0] == 0 && fixture.retained[1] == 2 &&
		fixture.retained[2] == 0);
	prepare_contacts(&fixture);
	fixture.pair[0][0] = fixture.pair[0][1] = 2;
	CHECK(mh_contact_schedule(2, 3, 2, visit_contact, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture.count == 7); /* Pair-only hits cause all three passes. */
	prepare_contacts(&fixture);
	fixture.track[0] = 7;
	CHECK(mh_contact_schedule(1, 1, 0, visit_contact, &fixture, NULL) ==
			MH_ERROR_NONE &&
		fixture.count == 4);
	CHECK(fixture.retained[0] == 18);
	prepare_contacts(&fixture);
	fixture.track[0] = 1;
	CHECK(mh_contact_schedule(1, 1, 0, visit_contact, &fixture, NULL) ==
			MH_ERROR_NONE &&
		fixture.count == 3);

	/* A later miss retains the earlier hit. */
	CHECK(fixture.retained[0] == 16);
	prepare_contacts(&fixture);
	fixture.fail = 2;
	CHECK(mh_contact_schedule(8, 255, 0, visit_contact, &fixture, NULL) !=
			MH_ERROR_NONE &&
		fixture.count == 2);
	prepare_contacts(&fixture);
	CHECK(mh_contact_schedule(MH_RACE_MAX_CARS + 1, 1, 0, visit_contact,
		&fixture, NULL) != MH_ERROR_NONE &&
		fixture.count == 0);
	CHECK(mh_contact_schedule(1, 2, 0, visit_contact, &fixture, NULL) !=
			MH_ERROR_NONE &&
		fixture.count == 0);
	CHECK(mh_contact_schedule(1, 1, 2, visit_contact, &fixture, NULL) !=
			MH_ERROR_NONE &&
		fixture.count == 0);
	CHECK(mh_contact_schedule(0, 0, 0, visit_contact, &fixture, NULL) ==
			MH_ERROR_NONE &&
		fixture.count == 0);
	prepare_contacts(&fixture);
	CHECK(mh_contact_schedule(13, 8191, 8190, visit_contact, &fixture,
		NULL) == MH_ERROR_NONE);
	CHECK(fixture.count == 14);

	/* Last peer is slot twelve. */
	CHECK((fixture.events[13] & 65535U) == 12);
	prepare_contacts(&fixture);
	CHECK(mh_contact_schedule(13, 8191, 4095, visit_contact, &fixture,
		NULL) == MH_ERROR_NONE);
	CHECK(fixture.count == 14 && ((fixture.events[1] >> 8) & 255U) == 12);
	puts("per-car contact passes, peer-only continuation, remote roles and failures passed");

	return 0;
}
