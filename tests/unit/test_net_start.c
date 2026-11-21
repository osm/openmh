#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/network/start.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
patched_timing(void)
{
	struct mh_net_start start, saved;
	unsigned long ping[MH_NET_PLAYERS];
	unsigned int due;

	memset(ping, 0, sizeof(ping));
	ping[1] = 1000;
	mh_net_start_reset(&start);
	CHECK(mh_net_start_step_for(MH_NET_WIRE_31_LOBBY, &start, 3, 2, ping,
		1000, &due, NULL) == MH_ERROR_NONE);
	CHECK(start.anchor_ms == 0 && due == 0);
	CHECK(mh_net_start_step_for(MH_NET_WIRE_31_LOBBY, &start, 3, 3, ping,
		1001.75, &due, NULL) == MH_ERROR_NONE);
	CHECK(start.anchor_ms == 1001 && start.started_ms == 0);
	CHECK(mh_net_start_step_for(MH_NET_WIRE_31_LOBBY, &start, 3, 3, ping,
		2501, &due, NULL) == MH_ERROR_NONE);
	CHECK(due == 0 && start.started_ms == 0);
	CHECK(mh_net_start_step_for(MH_NET_WIRE_31_LOBBY, &start, 3, 3, ping,
		2501.25, &due, NULL) == MH_ERROR_NONE);
	CHECK(due == 2 && start.sent_mask == 2 && start.started_ms == 2501);
	CHECK(mh_net_start_step_for(MH_NET_WIRE_31_LOBBY, &start, 3, 3, ping,
		3001.25, &due, NULL) == MH_ERROR_NONE);
	CHECK(due == 0 && start.sent_mask == 3 && start.started_ms == 2501);
	saved = start;
	due = 123;
	CHECK(mh_net_start_step_for((enum mh_net_wire)99, &start, 3, 3, ping,
		4000, &due, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&start, &saved, sizeof(start)) == 0 && due == 123);
}

int
main(void)
{
	struct mh_net_start start, saved;
	unsigned long ping[8], seed;
	unsigned int due, positions[8], mask, count, player, position_mask;

	patched_timing();
	memset(ping, 0, sizeof(ping));
	ping[1] = 100;
	ping[2] = 5000;
	mh_net_start_reset(&start);
	CHECK(mh_net_start_step(&start, 7, 3, ping, 1000, &due, NULL) ==
			MH_ERROR_NONE &&
		start.anchor_ms == 0 && due == 0);
	CHECK(mh_net_start_step(&start, 7, 7, ping, 1001.9, &due, NULL) ==
			MH_ERROR_NONE &&
		start.anchor_ms == 1001 && due == 0);
	CHECK(mh_net_start_step(&start, 7, 7, ping, 2001, &due, NULL) ==
			MH_ERROR_NONE &&
		due == 0);
	CHECK(mh_net_start_step(&start, 7, 7, ping, 2001.1, &due, NULL) ==
			MH_ERROR_NONE &&
		due == 4);
	CHECK(mh_net_start_step(&start, 7, 7, ping, 2951, &due, NULL) ==
			MH_ERROR_NONE &&
		due == 0);
	CHECK(mh_net_start_step(&start, 7, 7, ping, 2951.1, &due, NULL) ==
			MH_ERROR_NONE &&
		due == 2);
	CHECK(mh_net_start_step(&start, 7, 7, ping, 3001, &due, NULL) ==
			MH_ERROR_NONE &&
		start.started_ms == 0);
	CHECK(mh_net_start_step(&start, 7, 7, ping, 3001.1, &due, NULL) ==
			MH_ERROR_NONE &&
		start.started_ms == 3001);
	CHECK(mh_net_start_step(&start, 7, 7, ping, 4000, &due, NULL) ==
			MH_ERROR_NONE &&
		start.started_ms == 3001 && due == 0);
	saved = start;
	due = 123;
	CHECK(mh_net_start_step(&start, 7, 7, ping, 3999, &due, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&start, &saved, sizeof(start)) == 0 && due == 123);
	CHECK(mh_net_start_step(&start, 7, 7, ping, HUGE_VAL, &due, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_start_step(&start, 256, 7, ping, 4000, &due, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_start_step(&start, 6, 7, ping, 4000, &due, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_start_step(&start, 7, 256, ping, 4000, &due, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_start_step(NULL, 7, 7, ping, 4000, &due, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_start_step(&start, 7, 7, NULL, 4000, &due, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_start_step(&start, 7, 7, ping, 4000, NULL, NULL) !=
		MH_ERROR_NONE);
	mh_net_start_reset(NULL);
	mh_net_start_reset(&start);
	CHECK(mh_net_start_step(&start, 1, 0, ping, 0, &due, NULL) ==
			MH_ERROR_NONE &&
		start.anchor_ms == 0);
	CHECK(mh_net_start_step(&start, 1, 0, ping, 1, &due, NULL) ==
			MH_ERROR_NONE &&
		start.anchor_ms == 1);
	mh_net_start_reset(&start);
	ping[1] = 0;
	CHECK(mh_net_start_step(&start, 3, 3, ping, 4294967290.0, &due, NULL) ==
		MH_ERROR_NONE);

	/* Original u32 deadline wrap. */
	CHECK(start.started_ms == 4294967290UL && due == 2);

	for (mask = 1; mask <= 255; mask++) {
		seed = mask * 65537UL;
		count = 0;
		position_mask = 0;

		for (player = 0; player < 8; player++)
			if (mask & (1U << player))
				count++;

		CHECK(mh_net_grid(mask, &seed, positions, NULL) ==
			MH_ERROR_NONE);

		for (player = 0; player < 8; player++) {
			if (mask & (1U << player)) {
				CHECK(positions[player] >= 1 &&
					positions[player] <= count);
				CHECK(!(position_mask &
					(1U << positions[player])));
				position_mask |= 1U << positions[player];
			} else
				CHECK(positions[player] == 0);
		}
	}

	seed = 7;
	positions[0] = 77;
	CHECK(mh_net_grid(0, &seed, positions, NULL) != MH_ERROR_NONE &&
		seed == 7 && positions[0] == 77);
	CHECK(mh_net_grid(256, &seed, positions, NULL) != MH_ERROR_NONE);
	CHECK(mh_net_grid(1, NULL, positions, NULL) != MH_ERROR_NONE &&
		mh_net_grid(1, &seed, NULL, NULL) != MH_ERROR_NONE);

	return failures != 0;
}
