#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/demo/playback.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
write_word(unsigned char *bytes, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		bytes[i] = (unsigned char)((value >> (8 * i)) & 255UL);
}

int
main(void)
{
	unsigned char data[MH_DEMO_HEADER_BYTES + 65 * 248];
	struct mh_demo demo, invalid_demo;
	struct mh_demo_playback playback, saved_playback;
	struct mh_demo_playback_event event, saved_event;
	unsigned int i, j, mask, count, index;
	size_t size, stride;
	int ghost;

	memset(data, 0, sizeof(data));
	write_word(data, 0x40200000UL);
	write_word(data + 28, 1);
	write_word(data + 36, 1);
	size = MH_DEMO_HEADER_BYTES + 65 * 38;

	for (i = 0; i < 65; i++) {
		write_word(data + MH_DEMO_HEADER_BYTES + 38 * i, 257);
		write_word(
			data + MH_DEMO_HEADER_BYTES + 38 * i + 4, 100 + 10 * i);
	}

	CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_playback_begin(&demo, 0, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(playback.samples[0].active && playback.samples[0].unavailable &&
		playback.samples[0].world_velocity[0] == 0);
	CHECK(mh_demo_playback_step(&playback, 1000, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(event.started && !event.updated && playback.anchor_ms == 1000);
	CHECK(mh_demo_playback_step(&playback, 1099, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(!event.updated && !event.started && playback.active);
	CHECK(mh_demo_playback_step(&playback, 1100, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(event.updated && event.ring_index == 0 &&
		event.older_frame == 0 && event.newer_frame == 1);
	CHECK(mh_demo_playback_step(&playback, 1420, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(event.updated && event.ring_index == 32 &&
		playback.active_half == 1 && playback.eof &&
		playback.next_frame == 65);

	/* Partial fread retains the unwritten slots. */
	CHECK(playback.frame_indices[0] == 64 &&
		playback.frame_indices[1] == 1);
	CHECK(mh_demo_playback_step(&playback, 1739, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(event.updated && event.ring_index == 63 &&
		event.newer_frame == 64);
	CHECK(mh_demo_playback_step(&playback, 1740, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(event.ended && !playback.active && !event.updated);
	CHECK(mh_demo_playback_step(&playback, 2000, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(!event.ended && !event.updated && !event.started);
	CHECK(mh_demo_playback_begin(&demo, 0, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_playback_step(&playback, 0, &event, NULL) ==
			MH_ERROR_NONE &&
		event.started);
	CHECK(mh_demo_playback_step(&playback, 1, &event, NULL) ==
			MH_ERROR_NONE &&
		event.started && playback.target_ms == 0);
	CHECK(mh_demo_playback_step(&playback, 100000, &event, NULL) ==
			MH_ERROR_NONE &&
		event.ended && playback.next_frame == 64);
	CHECK(mh_demo_playback_begin(&demo, 0, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_playback_step(&playback, 1, &event, NULL) ==
		MH_ERROR_NONE);
	memcpy(&saved_playback, &playback, sizeof(playback));
	memcpy(&saved_event, &event, sizeof(event));
	CHECK(mh_demo_playback_step(&playback, 0, &event, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_playback_step(NULL, 2, &event, NULL) != MH_ERROR_NONE);
	CHECK(mh_demo_playback_step(&playback, 2, NULL, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&playback, &saved_playback, sizeof(playback)) == 0 &&
		memcmp(&event, &saved_event, sizeof(event)) == 0);
	write_word(data + MH_DEMO_HEADER_BYTES + 38 * 64 + 4, 100);
	CHECK(mh_demo_playback_begin(&demo, 0, &playback, NULL) !=
		MH_ERROR_NONE);
	write_word(data + MH_DEMO_HEADER_BYTES + 38 * 64 + 4, 740);
	write_word(data + MH_DEMO_HEADER_BYTES + 38 * 64 + 26, 0x7f800000UL);
	CHECK(mh_demo_playback_begin(&demo, 0, &playback, NULL) !=
		MH_ERROR_NONE);
	write_word(data + MH_DEMO_HEADER_BYTES + 38 * 64 + 26, 0);
	invalid_demo = demo;
	invalid_demo.players[0].active = 0;
	invalid_demo.players[7].active = 1;
	CHECK(mh_demo_playback_begin(&invalid_demo, 0, &playback, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_playback_begin(&demo, 2, &playback, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_playback_begin(NULL, 0, &playback, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_playback_begin(&demo, 0, NULL, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&playback, &saved_playback, sizeof(playback)) == 0);

	/* Eight-body disk stride, seven ghost samples, all unavailable. */
	memset(data, 0, sizeof(data));
	write_word(data, 0x40200000UL);
	write_word(data + 28, 8);
	stride = 248;

	for (i = 0; i < 8; i++)
		write_word(data + 36 + 65 * i, 1);

	for (i = 0; i < 65; i++) {
		write_word(data + MH_DEMO_HEADER_BYTES + stride * i, 255);
		write_word(data + MH_DEMO_HEADER_BYTES + stride * i + 4,
			100 + 10 * i);

		for (j = 0; j < 8; j++)
			data[MH_DEMO_HEADER_BYTES + stride * i + 8 + 30 * j +
				5] = (unsigned char)(10 * j + i);
	}

	CHECK(mh_demo_read(data, sizeof(data), &demo, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_playback_begin(&demo, 1, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.sample_count == 7);
	CHECK(mh_demo_playback_step(&playback, 1000, &event, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_playback_step(&playback, 1105, &event, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 7; i++)
		CHECK(playback.samples[i].active &&
			playback.samples[i].unavailable &&
			playback.samples[i].vehicle.rpm == 625 * i + 31.25);

	CHECK(playback.samples[7].vehicle.rpm == 0 &&
		!playback.samples[7].active);

	/*
	 * Sparse network files keep original flags/identities but pack only
	 * initially present bodies. Exercise every mask in watch and Ghost.
	 */
	for (mask = 1; mask < 256; mask++) {
		memset(data, 0, sizeof(data));
		write_word(data, 0x40200000UL);
		count = 0;

		for (i = 0; i < 8; i++)
			if (mask & (1U << i)) {
				count++;
				write_word(data + 36 + 65 * i, 1);
				write_word(data + 32, i);
			}

		write_word(data + 28, count);
		stride = 8 + 30 * count;

		for (i = 0; i < 65; i++) {
			write_word(
				data + MH_DEMO_HEADER_BYTES + stride * i, mask);
			write_word(data + MH_DEMO_HEADER_BYTES + stride * i + 4,
				100 + 10 * i);
			index = 0;

			for (j = 0; j < 8; j++)
				if (mask & (1U << j)) {
					data[MH_DEMO_HEADER_BYTES + stride * i +
						8 + 30 * index + 5] =
						(unsigned char)(10 * j + i);
					index++;
				}
		}

		CHECK(mh_demo_read(data, MH_DEMO_HEADER_BYTES + 65 * stride,
			&demo, NULL) == MH_ERROR_NONE);

		for (ghost = 0; ghost <= 1; ghost++) {
			CHECK(mh_demo_playback_begin(&demo, ghost, &playback,
				NULL) == MH_ERROR_NONE);
			CHECK(mh_demo_playback_step(&playback, 1000, &event,
				NULL) == MH_ERROR_NONE);
			CHECK(mh_demo_playback_step(&playback, 1105, &event,
				NULL) == MH_ERROR_NONE);
			index = 0;

			for (j = 0; j < 8; j++) {
				const struct mh_demo_sample *sample;

				sample = &playback.samples[j];

				if ((mask & (1U << j)) &&
					index < playback.sample_count) {
					CHECK(sample->active &&
						sample->unavailable == ghost);
					CHECK(sample->vehicle.rpm ==
						625 * j + 31.25);
					index++;
				} else
					CHECK(!sample->active &&
						sample->vehicle.rpm == 0);
			}
		}
	}

	puts("demo playback: startup, refill/EOF, ghost cap, every sparse roster and atomic validation passed");

	return 0;
}
