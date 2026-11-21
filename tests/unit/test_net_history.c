#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/history.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
receive_vehicle(struct mh_net_history *history, unsigned long stamp, double now)
{
	struct mh_net_vehicle vehicle;
	unsigned char body[30];
	unsigned int wheel;
	int accepted;

	memset(&vehicle, 0, sizeof(vehicle));

	for (wheel = 0; wheel < 4; wheel++)
		vehicle.extension[wheel] = 1;

	vehicle.rpm = 7000;
	vehicle.gear = 4;
	vehicle.position[0] = (stamp % 10000) * .01;
	CHECK(mh_net_vehicle_encode(&vehicle, body, 30, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_history_receive(history, stamp, body, 30, now, &accepted,
		NULL) == MH_ERROR_NONE &&
		accepted);
}

static void
warmup_and_anchors(void)
{
	struct mh_net_history history;
	struct mh_net_playback playback, previous;
	unsigned int sample;

	mh_net_history_reset(&history);
	CHECK(mh_net_history_sample(&history, 0, &playback, NULL) ==
			MH_ERROR_NONE &&
		!playback.ready && playback.stale);

	for (sample = 1; sample <= 10; sample++) {
		receive_vehicle(&history, sample * 100, sample * 50);
		CHECK(mh_net_history_sample(&history, sample * 50, &playback,
			NULL) == MH_ERROR_NONE);
		CHECK(playback.ready == (sample == 10));
	}

	CHECK(playback.vehicle.position[0] == 8 &&
		history.local_anchor == 500 && history.remote_anchor == 800);
	CHECK(playback.world_velocity[0] == 10 && playback.updated &&
		playback.older_index == 2 && !playback.stale);
	CHECK(mh_net_history_sample(&history, 550, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.vehicle.position[0] == 8.5);
	CHECK(mh_net_history_sample(&history, 750, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.vehicle.position[0] == 10.5);
	CHECK(playback.older_index == 1 && history.local_anchor == 750 &&
		history.remote_anchor == 800);
	CHECK(mh_net_history_sample(&history, 751, &playback, NULL) ==
			MH_ERROR_NONE &&
		fabs(playback.vehicle.position[0] - 8.01) < .000001);
	previous = playback;

	for (sample = 0; sample < 10; sample++)
		receive_vehicle(&history, 3000 + sample * 100, 752);

	CHECK(mh_net_history_sample(&history, 753, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.ready && !playback.updated &&
		playback.older_index == 10);
	CHECK(memcmp(&playback.vehicle, &previous.vehicle,
		sizeof(playback.vehicle)) == 0);
	CHECK(mh_net_history_sample(&history, 1752, &playback, NULL) ==
			MH_ERROR_NONE &&
		!playback.stale);
	CHECK(mh_net_history_sample(&history, 1752.001, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.stale);
	mh_net_history_reset(&history);

	for (sample = 1; sample <= 10; sample++)
		receive_vehicle(&history, sample * 100, sample * 50);

	CHECK(mh_net_history_sample(&history, 500, &playback, NULL) ==
		MH_ERROR_NONE);

	for (sample = 11; sample <= 15; sample++)
		receive_vehicle(&history, sample * 100, 500.125);

	CHECK(mh_net_history_sample(&history, 500.125, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.older_index == 7);
	CHECK(playback.vehicle.position[0] == 8 &&
		history.local_anchor == 500 && history.remote_anchor == 1200);
}

static void
ownership_of_time(void)
{
	struct mh_net_history history, before;
	struct mh_net_playback playback, saved;
	struct mh_net_vehicle vehicle;
	unsigned char body[30];
	int accepted;
	unsigned int stamp;

	mh_net_history_reset(&history);
	receive_vehicle(&history, 100, .125);
	CHECK(history.received_ms == 0);
	memset(&vehicle, 0, sizeof(vehicle));
	vehicle.rpm = 7000;
	vehicle.gear = 4;
	CHECK(mh_net_vehicle_encode(&vehicle, body, 30, NULL) == MH_ERROR_NONE);
	before = history;

	for (stamp = 0; stamp <= 100; stamp++) {
		accepted = 9;
		CHECK(mh_net_history_receive(&history, stamp, body, 30, 1000,
			&accepted, NULL) == MH_ERROR_NONE &&
			!accepted);
		CHECK(memcmp(&history, &before, sizeof(history)) == 0);
	}

	CHECK(mh_net_history_sample(&history, 1000.001, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.stale);
	before = history;
	saved = playback;
	accepted = 9;
	CHECK(mh_net_history_receive(&history, 101, body, 29, 1001, &accepted,
		NULL) != MH_ERROR_NONE &&
		accepted == 9);
	CHECK(memcmp(&history, &before, sizeof(history)) == 0);
	CHECK(mh_net_history_sample(&history, 999, &playback, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved, &playback, sizeof(playback)) == 0);
	CHECK(mh_net_history_sample(&history, HUGE_VAL, &playback, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&history, &before, sizeof(history)) == 0);
	vehicle.position[0] = 1000001;
	CHECK(mh_net_vehicle_encode(&vehicle, body, 30, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_history_receive(&history, 101, body, 30, 1001, &accepted,
		NULL) != MH_ERROR_NONE &&
		accepted == 9);
	CHECK(memcmp(&history, &before, sizeof(history)) == 0);
	vehicle.position[0] = 0;
	CHECK(mh_net_vehicle_encode(&vehicle, body, 30, NULL) == MH_ERROR_NONE);
	receive_vehicle(&history, MH_U32_MAX, 1001);
	before = history;
	CHECK(mh_net_history_receive(&history, 1, body, 30, 1002, &accepted,
		NULL) == MH_ERROR_NONE &&
		!accepted);
	CHECK(memcmp(&history, &before, sizeof(history)) == 0);
	mh_net_history_reset(&history);
	CHECK(mh_net_history_receive(&history, 1, body, 30, 0, &accepted,
		NULL) == MH_ERROR_NONE &&
		accepted);
	CHECK(mh_net_history_sample(NULL, 0, &playback, NULL) !=
			MH_ERROR_NONE &&
		mh_net_history_sample(&history, 0, NULL, NULL) !=
			MH_ERROR_NONE);
}

static void
wrapping_and_discrete(void)
{
	struct mh_net_history history;
	struct mh_net_playback playback;
	struct mh_net_vehicle vehicle;
	unsigned char body[30];
	unsigned int sample, variant;
	int accepted;

	for (variant = 0; variant < 2; variant++) {
		mh_net_history_reset(&history);
		memset(&vehicle, 0, sizeof(vehicle));

		for (sample = 1; sample <= 10; sample++) {
			vehicle.angles[0] =
				sample >= 9 ? (variant ? 4 : 3.875) : 0;
			vehicle.gear = sample >= 9 ? 5 : 2;
			vehicle.flags = sample >= 9 ? 1 : 0;
			CHECK(mh_net_vehicle_encode(&vehicle, body, 30, NULL) ==
				MH_ERROR_NONE);
			CHECK(mh_net_history_receive(&history, sample * 100,
				body, 30, sample * 50, &accepted,
				NULL) == MH_ERROR_NONE &&
				accepted);
		}

		CHECK(mh_net_history_sample(&history, 500, &playback, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_net_history_sample(&history, 550, &playback, NULL) ==
			MH_ERROR_NONE);

		/* Use the newer discrete values, even before halfway. */
		CHECK(playback.vehicle.gear == 5 &&
			playback.vehicle.flags == 1);

		if (variant) {
			CHECK(fabs(playback.vehicle.angles[0] -
				5.141592741012573) < .000001);
			CHECK(playback.euler_rate[0] < 0);
		} else {
			CHECK(playback.vehicle.angles[0] == 1.9375 &&
				playback.euler_rate[0] == 38.75);
		}
	}

	mh_net_history_reset(&history);

	for (sample = 0; sample < 10; sample++)
		receive_vehicle(&history, MH_U32_MAX - 1000 + sample * 100,
			sample * 10);

	CHECK(mh_net_history_sample(&history, 90, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.ready);
	CHECK(mh_net_history_sample(&history, 500, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.target_stamp == 109 && !playback.updated);
}

static void
patched_prediction(void)
{
	struct mh_net_history history, saved;
	struct mh_net_playback playback, before;
	struct mh_net_prediction limits;
	unsigned int sample;

	mh_net_history_reset(&history);
	limits.minimum = 0;
	limits.maximum = 0;
	CHECK(mh_net_history_predict(&history, &limits, 0, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(limits.minimum == 1 && limits.maximum == 1 && !playback.ready);
	limits.minimum = 8;
	limits.maximum = 2;
	CHECK(mh_net_history_predict(&history, &limits, 0, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(limits.minimum == 2 && limits.maximum == 2);

	limits.minimum = 1;
	limits.maximum = 5;

	for (sample = 1; sample <= 10; sample++)
		receive_vehicle(&history, sample * 100, sample * 50);

	CHECK(mh_net_history_predict(&history, &limits, 500, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(playback.ready && playback.updated && playback.older_index == 3 &&
		playback.vehicle.position[0] == 7 &&
		history.remote_anchor == 700);
	before = playback;
	CHECK(mh_net_history_predict(&history, &limits, 800, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(!playback.updated && playback.older_index == 10 &&
		history.local_anchor == 500 &&
		!memcmp(&playback.vehicle, &before.vehicle,
			sizeof(playback.vehicle)));

	/* A completely new window reanchors once, then resumes next sample. */
	for (sample = 0; sample < 10; sample++)
		receive_vehicle(&history, 3000 + sample * 100, 801);

	CHECK(mh_net_history_predict(&history, &limits, 802, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(!playback.updated && history.local_anchor == 802 &&
		history.remote_anchor == 3600);
	CHECK(mh_net_history_predict(&history, &limits, 802, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(playback.updated && playback.vehicle.position[0] == 36);

	/* Invalid calls preserve normalized preferences as well as playback. */
	saved = history;
	before = playback;
	limits.minimum = 0;
	limits.maximum = 0;
	CHECK(mh_net_history_predict(&history, &limits, 801, &playback, NULL) !=
		MH_ERROR_NONE);
	CHECK(!memcmp(&history, &saved, sizeof(history)) &&
		!memcmp(&playback, &before, sizeof(playback)) &&
		limits.minimum == 0 && limits.maximum == 0);

	/* CFG-only 10/10 must select an actual entry, not adjacent memory. */
	mh_net_history_reset(&history);

	for (sample = 1; sample <= 10; sample++)
		receive_vehicle(&history, sample * 100, sample * 50);

	limits.minimum = limits.maximum = 10;
	CHECK(mh_net_history_predict(&history, &limits, 500, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(playback.updated && playback.vehicle.position[0] == 1 &&
		history.remote_anchor == 100);
}

int
main(void)
{
	patched_prediction();
	warmup_and_anchors();
	ownership_of_time();
	wrapping_and_discrete();

	return failures != 0;
}
