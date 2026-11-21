#include <stdio.h>
#include <string.h>

#include "engine/audio/crash.h"
#include "engine/audio/sound_pool.h"
#include "engine/core/error.h"

#define FIXTURE_RECORDS 48

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
configure(struct mh_sound_pool *pool, unsigned int id, unsigned long priority,
	float left, float right, unsigned long flags, int eligible)
{
	struct mh_sound_request request;

	memset(&request, 0, sizeof(request));
	request.ready = 1;
	request.eligible = eligible;
	request.flags = flags;
	request.priority = priority;
	request.left = left;
	request.right = right;
	CHECK(mh_sound_pool_configure(pool, id, &request, NULL) ==
		MH_ERROR_NONE);
}

static void
policy(void)
{
	struct mh_sound_pool pool, saved;
	struct mh_sound_changes changes, before;
	unsigned int i, stopped, mask;

	CHECK(mh_sound_pool_init(FIXTURE_RECORDS, &pool, NULL) ==
		MH_ERROR_NONE);

	for (i = 1; i <= FIXTURE_RECORDS; i++)
		configure(&pool, i, 1, 1, 1, 0, 1);

	for (i = 1; i <= 33; i++)
		CHECK(mh_sound_pool_request(&pool, i, &stopped, NULL) ==
				MH_ERROR_NONE &&
			stopped == 0);

	CHECK(pool.pending[0] == 1 && pool.pending[31] == 32 &&
		!pool.records[32].active);
	CHECK(mh_sound_pool_stop(&pool, 3, &stopped, NULL) == MH_ERROR_NONE &&
		stopped == 0);
	CHECK(mh_sound_pool_request(&pool, 33, &stopped, NULL) ==
			MH_ERROR_NONE &&
		pool.pending[2] == 33);
	CHECK(mh_sound_pool_request(&pool, 1, &stopped, NULL) ==
			MH_ERROR_NONE &&
		pool.pending[0] == 1);
	CHECK(mh_sound_pool_update(&pool, 0, &changes, NULL) == MH_ERROR_NONE &&
		changes.count == 16);
	CHECK(pool.channels[0] == 1 && pool.channels[2] == 33 &&
		pool.channels[7] == 8);
	CHECK(!pool.records[8].active && pool.records[0].score == 1.5f);

	/*
	 * Equal scores cannot steal. A strict increase replaces the first of
	 * several equal minima, regardless of record ID.
	 */
	configure(&pool, 9, 1, 1, 1, 0, 1);
	CHECK(mh_sound_pool_request(&pool, 9, &stopped, NULL) == MH_ERROR_NONE);
	CHECK(mh_sound_pool_update(&pool, 255, &changes, NULL) ==
			MH_ERROR_NONE &&
		changes.count == 8);
	CHECK(!pool.records[8].active && pool.channels[0] == 1);
	configure(&pool, 9, 2, 0, 0, 0, 1);
	CHECK(mh_sound_pool_request(&pool, 9, &stopped, NULL) == MH_ERROR_NONE);
	CHECK(mh_sound_pool_update(&pool, 255, &changes, NULL) ==
			MH_ERROR_NONE &&
		changes.count == 10);
	CHECK(pool.channels[0] == 9 && !pool.records[0].active);
	CHECK(changes.entries[0].kind == MH_SOUND_STOP &&
		changes.entries[0].record == 1);
	CHECK(changes.entries[1].kind == MH_SOUND_START &&
		changes.entries[1].record == 9);

	/*
	 * Resident parameters/completion are refreshed after requests. A sound
	 * can be declined before an inaudible/finished resident frees a
	 * channel.
	 */
	configure(&pool, 9, 0, 0, 0, 0, 0);
	CHECK(mh_sound_pool_request(&pool, 10, &stopped, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_sound_pool_update(&pool, 253, &changes, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.channels[0] == 0 && pool.channels[1] == 0 &&
		!pool.records[9].active);
	CHECK(mh_sound_pool_request(&pool, 10, &stopped, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_sound_pool_update(&pool, 252, &changes, NULL) ==
			MH_ERROR_NONE &&
		pool.channels[0] == 10);

	/* Lowest free channel wins even when a later resident is weaker. */
	configure(&pool, 11, 0, -5, 0, 0, 1);
	CHECK(mh_sound_pool_request(&pool, 11, &stopped, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_sound_pool_update(&pool, 255, &changes, NULL) ==
			MH_ERROR_NONE &&
		pool.channels[1] == 11);
	CHECK(pool.records[10].score == 0 && pool.records[10].active);

	/*
	 * A retrigger stops its channel even if 32 unrelated requests fill the
	 * queue. Queued retriggers instead free and reuse their first hole.
	 */
	for (i = 12; i < 45; i++)
		CHECK(mh_sound_pool_request(&pool, i, &stopped, NULL) ==
			MH_ERROR_NONE);

	configure(&pool, 10, 1, 1, 1, 4, 1);
	CHECK(mh_sound_pool_request(&pool, 10, &stopped, NULL) ==
			MH_ERROR_NONE &&
		stopped == 1);
	CHECK(!pool.records[9].active && pool.channels[0] == 0);
	configure(&pool, 12, 1, 1, 1, 4, 1);
	CHECK(mh_sound_pool_request(&pool, 12, &stopped, NULL) ==
			MH_ERROR_NONE &&
		stopped == 0 && pool.pending[0] == 12);
	CHECK(mh_sound_pool_clear(&pool, &mask, NULL) == MH_ERROR_NONE &&
		mask == 254);

	for (i = 0; i < pool.count; i++)
		CHECK(!pool.records[i].active && pool.records[i].channel == 0);

	CHECK(pool.records[0].score == 1.5f &&
		pool.records[9].request.flags == 4);
	CHECK(mh_sound_pool_stop(&pool, 0, &stopped, NULL) == MH_ERROR_NONE &&
		stopped == 0);

	/* Replacing all channels repeatedly fills the bounded event array. */
	for (i = 1; i <= 8; i++)
		CHECK(mh_sound_pool_request(&pool, i, &stopped, NULL) ==
			MH_ERROR_NONE);

	CHECK(mh_sound_pool_update(&pool, 0, &changes, NULL) == MH_ERROR_NONE);

	for (i = 9; i <= 40; i++) {
		configure(&pool, i, i, 5, -1, 0, 1);
		CHECK(mh_sound_pool_request(&pool, i, &stopped, NULL) ==
			MH_ERROR_NONE);
	}

	CHECK(mh_sound_pool_update(&pool, 255, &changes, NULL) ==
			MH_ERROR_NONE &&
		changes.count == MH_SOUND_CHANGES);
	CHECK(pool.records[39].score == 40.5f);

	/* Bad state/arguments preserve both the pool and output. */
	saved = pool;
	before = changes;
	CHECK(mh_sound_pool_update(&pool, 256, &changes, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved, sizeof(pool)) == 0 &&
		memcmp(&changes, &before, sizeof(changes)) == 0);
	CHECK(mh_sound_pool_init(MH_SOUND_RECORDS + 1, &pool, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&pool, &saved, sizeof(pool)) == 0);
	{
		struct mh_sound_request invalid;

		invalid = pool.records[0].request;
		invalid.ready = 2;
		CHECK(mh_sound_pool_configure(&pool, 1, &invalid, NULL) !=
			MH_ERROR_NONE);
		invalid.ready = 1;
		invalid.left = 1e7f;
		CHECK(mh_sound_pool_configure(&pool, 1, &invalid, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&pool, &saved, sizeof(pool)) == 0);
	}

	pool.pending[0] = 1000;
	saved = pool;
	stopped = 999;
	CHECK(mh_sound_pool_stop(&pool, 1, &stopped, NULL) != MH_ERROR_NONE &&
		stopped == 999);
	CHECK(mh_sound_pool_request(&pool, 1, &stopped, NULL) !=
			MH_ERROR_NONE &&
		stopped == 999);
	CHECK(mh_sound_pool_clear(&pool, &stopped, NULL) != MH_ERROR_NONE &&
		stopped == 999);
	CHECK(mh_sound_pool_update(&pool, 255, &changes, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved, sizeof(pool)) == 0 &&
		memcmp(&changes, &before, sizeof(changes)) == 0);
}

int
main(void)
{

	policy();

	return failures != 0;
}
