#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/record.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_demo_recorder recorder, saved_recorder;
static struct mh_demo_record_event event, saved_event;
static struct mh_demo header;
static struct mh_demo_player players[MH_DEMO_MAX_PLAYERS];
static struct mh_demo_record_sample sample;
static unsigned char input[MH_DEMO_HEADER_BYTES + 64 * 248],
	encoded[MH_DEMO_HEADER_BYTES];

int
main(void)
{
	unsigned int i, mask, count;
	unsigned long clock_ms;

	memset(&header, 0, sizeof(header));
	strcpy(header.track, "Fixture");
	header.laps = 1;
	memset(&sample, 0, sizeof(sample));

	for (i = 0; i < 8; i++)
		memset(sample.bodies[i], (int)i + 1, 30);

	for (mask = 1; mask < 256; mask++) {
		count = 0;

		for (i = 0; i < 8; i++) {
			header.players[i].active = (mask >> i) & 1U;

			if (header.players[i].active) {
				count++;
				header.local_player = i;
			}
		}

		header.player_count = count;
		sample.active_mask = 255;
		sample.unavailable_mask = 255;
		CHECK(mh_demo_record_begin(&header, &recorder, NULL) ==
			MH_ERROR_NONE);
		CHECK(recorder.header.data == NULL &&
			recorder.initial_mask == mask);
		CHECK(mh_demo_record_step(&recorder, 0, &sample, &event,
			NULL) == MH_ERROR_NONE &&
			event.started && event.frames == 0);
		CHECK(mh_demo_record_step(&recorder, 1, &sample, &event,
			NULL) == MH_ERROR_NONE &&
			event.started);
		CHECK(mh_demo_record_step(&recorder, 63, &sample, &event,
			NULL) == MH_ERROR_NONE &&
			event.frames == 0);
		CHECK(mh_demo_record_step(&recorder, 64, &sample, &event,
			NULL) == MH_ERROR_NONE &&
			event.frames == 1 && event.size == 0);
		CHECK(mh_read_u32le(recorder.ring) ==
			(unsigned long)(mask | (mask << 8)));
		CHECK(mh_read_u32le(recorder.ring + 4) == 63 &&
			recorder.frame_count == 1);
		sample.active_mask = 0;

		for (i = 2; i <= 32; i++)
			CHECK(mh_demo_record_step(&recorder, 1 + 63 * i,
				&sample, &event, NULL) == MH_ERROR_NONE &&
				event.frames == 1);

		CHECK(event.size == 32 * (8 + 30 * count) &&
			recorder.ring_index == 32);
		CHECK(mh_read_u32le(event.data + 8 + 30 * count) ==
			(unsigned long)(mask << 8));
		CHECK(mh_demo_record_step(&recorder, 2080, &sample, &event,
			NULL) == MH_ERROR_NONE &&
			event.frames == 1);
		sample.active_mask = mask;
		CHECK(mh_demo_record_finish(&recorder, &sample, NULL, 0, &event,
			NULL) == MH_ERROR_NONE);
		CHECK(event.frames == 31 && recorder.frame_count == 64 &&
			recorder.ring_index == 0 && recorder.closed);
		CHECK(event.size == 32 * (8 + 30 * count));

		/* Padding drops unavailable. */
		CHECK(mh_read_u32le(event.data + 8 + 30 * count) == mask);
		CHECK(mh_read_u32le(event.data + event.size - (8 + 30 * count) +
			4) == 2110);
		CHECK(mh_demo_record_step(&recorder, 2200, &sample, &event,
			NULL) != MH_ERROR_NONE);
		CHECK(mh_demo_record_finish(&recorder, &sample, NULL, 0, &event,
			NULL) != MH_ERROR_NONE);
	}

	/*
	 * Short recordings keep their true length; the file layer must not
	 * pretend they have the original loader's 64-frame startup block.
	 */
	CHECK(mh_demo_record_begin(&header, &recorder, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_record_finish(&recorder, &sample, NULL, 0, &event,
		NULL) == MH_ERROR_NONE &&
		event.frames == 0 && event.size == 0);
	CHECK(recorder.frame_count == 0);
	CHECK(mh_demo_record_begin(&header, &recorder, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_record_step(&recorder, 100, &sample, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_record_step(&recorder, 100000, &sample, &event, NULL) ==
			MH_ERROR_NONE &&
		event.frames == 1);
	CHECK(mh_demo_record_finish(&recorder, &sample, NULL, 0, &event,
		NULL) == MH_ERROR_NONE &&
		recorder.frame_count == 32);

	/*
	 * Finalization uses current bodies and copies non-null progress tables
	 * even for an inactive entrant. Missing tables keep their initial
	 * bytes.
	 */
	header.players[7].crossings[2][3] = 77;
	CHECK(mh_demo_record_begin(&header, &recorder, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_record_step(&recorder, 100, &sample, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_record_step(&recorder, 163, &sample, &event, NULL) ==
		MH_ERROR_NONE);
	sample.active_mask = 0;
	memset(sample.bodies[7], 0xcc, 30);
	players[0].crossings[26][3] = MH_U32_MAX;
	CHECK(mh_demo_record_finish(&recorder, &sample, players, 1, &event,
		NULL) == MH_ERROR_NONE);
	CHECK(recorder.header.players[0].crossings[26][3] == MH_U32_MAX &&
		recorder.header.players[7].crossings[2][3] == 77);
	CHECK(event.data[2 * 248 - 1] == 0xcc &&
		mh_read_u32le(event.data + 248) == 0);

	/* The ordinary threshold does not wrap; padding timestamps do. */
	clock_ms = 0xffffff00UL;
	CHECK(mh_demo_record_begin(&header, &recorder, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_record_step(&recorder, clock_ms, &sample, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_record_step(&recorder, MH_U32_MAX, &sample, &event,
		NULL) == MH_ERROR_NONE &&
		event.frames == 1);
	CHECK(mh_demo_record_step(&recorder, MH_U32_MAX, &sample, &event,
		NULL) == MH_ERROR_NONE &&
		event.frames == 0);
	CHECK(mh_demo_record_finish(&recorder, &sample, NULL, 0, &event,
		NULL) == MH_ERROR_NONE &&
		recorder.last_frame_ms == 30);
	CHECK(mh_read_u32le(event.data + 31 * 248 + 4) == 286);
	CHECK(mh_demo_record_begin(&header, &recorder, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_record_step(&recorder, 100, &sample, &event, NULL) ==
		MH_ERROR_NONE);
	saved_recorder = recorder;
	saved_event = event;
	CHECK(mh_demo_record_step(&recorder, 99, &sample, &event, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_record_step(&recorder, 200, NULL, &event, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_record_step(&recorder, 200, &sample, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_record_finish(&recorder, &sample, NULL, 1, &event,
		NULL) != MH_ERROR_NONE);
	sample.active_mask = 256;
	CHECK(mh_demo_record_step(&recorder, 200, &sample, &event, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&recorder, &saved_recorder, sizeof(recorder)) == 0 &&
		memcmp(&event, &saved_event, sizeof(event)) == 0);
	sample.active_mask = 255;

	if (ULONG_MAX > MH_U32_MAX) {
		players[7].crossings[26][3] = ULONG_MAX;
		CHECK(mh_demo_record_finish(&recorder, &sample, players, 128,
			&event, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&recorder, &saved_recorder, sizeof(recorder)) ==
				0 &&
			memcmp(&event, &saved_event, sizeof(event)) == 0);
	}

	memset(encoded, 0xa5, sizeof(encoded));
	memcpy(input, encoded, sizeof(encoded));
	CHECK(mh_demo_header_write(&header, encoded, sizeof(encoded) - 1,
		NULL) != MH_ERROR_NONE);
	header.player_count = 0;
	CHECK(mh_demo_record_begin(&header, &recorder, NULL) != MH_ERROR_NONE &&
		memcmp(&recorder, &saved_recorder, sizeof(recorder)) == 0);
	CHECK(mh_demo_header_write(&header, encoded, sizeof(encoded), NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(encoded, input, sizeof(encoded)) == 0);
	puts("demo recorder: all rosters, strict cadence, blocks, sparse bodies, final padding/tables, wrap and atomic errors passed");

	return 0;
}
