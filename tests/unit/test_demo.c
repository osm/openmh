#include <stdio.h>
#include <string.h>

#include "engine/assets/demo.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
write_word(unsigned char *cursor, unsigned long word)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		cursor[i] = (unsigned char)((word >> (i * 8)) & 255UL);
}

static size_t
write_fixture(unsigned char *data, unsigned int mask, int patched)
{
	unsigned int i, j, k, count, local, slots, player_bytes, color_offset,
		crossings_offset;
	size_t header_bytes;
	size_t stride, frame_index, body;
	unsigned char *cursor;

	slots = patched ? 13 : 8;
	player_bytes = patched ? 81 : 65;
	color_offset = patched ? 57 : 41;
	crossings_offset = patched == 2 ? 1097 : patched ? 1089 : 556;
	header_bytes = patched == 2 ? 6713 : patched ? 6705 : 4012;
	count = 0;
	local = 0;

	for (i = 0; i < slots; i++)
		if ((mask & (1U << i)) != 0) {
			count++;
			local = i;
		}

	stride = 8 + 30 * count;
	memset(data, 0, header_bytes + 65 * stride);
	write_word(data,
		patched == 2	  ? 0x40800000UL
			: patched ? 0x40400000UL
				: 0x40200000UL);

	if (patched == 2) {
		write_word(data + 1089, 1);
		write_word(data + 1093, 120);
	}

	write_word(data + 4, 0x87654321UL);
	memcpy(data + 8, "ABCDEFGHIJKLMNOP", 16);
	write_word(data + 24, 25);
	write_word(data + 28, count);
	write_word(data + 32, local);

	for (i = 0; i < slots; i++) {
		cursor = data + 36 + player_bytes * i;

		if ((mask & (1U << i)) != 0)
			write_word(cursor, i + 1);

		memset(cursor + 4, 'a' + i, 16);
		memset(cursor + 20, 'A' + i, 5);
		memset(cursor + 25, '0' + i, 16);

		if (patched)
			memset(cursor + 41, 'k' + i, 16);

		for (j = 0; j < 6; j++)
			write_word(cursor + color_offset + 4 * j,
				0xfedc0000UL + 100 * i + j);

		for (j = 0; j < 27; j++)
			for (k = 0; k < 4; k++)
				write_word(data + crossings_offset + 432 * i +
						16 * j + 4 * k,
					0xf0000000UL + 1000 * i + 10 * j + k);
	}

	for (frame_index = 0; frame_index < 65; frame_index++) {
		cursor = data + header_bytes + frame_index * stride;

		/*
		 * Frame masks change without changing the original
		 * roster/stride. Preserve unknown flags and clock wrap at the
		 * container boundary.
		 */
		write_word(cursor,
			(patched ? 0xfc000000UL : 0xbeef0000UL) |
				((unsigned long)mask << slots) |
				((frame_index & 1) != 0 ? 0 : mask));
		write_word(cursor + 4,
			frame_index == 0 ? MH_U32_MAX
					: (unsigned long)frame_index);
		body = 8;

		for (i = 0; i < slots; i++)
			if ((mask & (1U << i)) != 0) {
				memset(cursor + body, (int)(i + frame_index),
					30);
				body += 30;
			}
	}

	return header_bytes + 65 * stride;
}

static size_t
write_legacy_fixture(unsigned char *data, unsigned int mask)
{
	return write_fixture(data, mask, 0);
}

static int
native_rate_roundtrip(unsigned int rate)
{
	unsigned char data[6713 + 65 * 398], encoded[6714];
	struct mh_demo demo;
	struct mh_demo_frame frame;
	size_t size;

	size = write_fixture(data, 8191, 2);
	write_word(data + 1093, rate);
	CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_NONE);
	CHECK(demo.version == MH_DEMO_VERSION_40 && demo.timing_version == 1 &&
		demo.simulation_rate == rate);
	CHECK(mh_demo_frame_read(&demo, 64, &frame, NULL) == MH_ERROR_NONE);
	CHECK(frame.vehicles[12] == data + 6713 + 64 * 398 + 8 + 12 * 30);
	memset(encoded, 0xa5, sizeof(encoded));
	CHECK(mh_demo_header_write(&demo, encoded, sizeof(encoded), NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(data, encoded, 6713) == 0 && encoded[6713] == 0xa5);

	return 0;
}

static int
native_timing_rejections(void)
{
	unsigned char data[6713 + 65 * 398], encoded[6714];
	struct mh_demo demo, saved;
	size_t size;

	size = write_fixture(data, 8191, 2);
	CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_NONE);
	saved = demo;
	write_word(data + 1089, 2);
	CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&demo, &saved, sizeof(demo)) == 0);
	write_word(data + 1089, 1);
	write_word(data + 1093, 0);
	CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&demo, &saved, sizeof(demo)) == 0);
	demo.version = MH_DEMO_VERSION_30;
	CHECK(mh_demo_header_write(&demo, encoded, sizeof(encoded), NULL) ==
		MH_ERROR_INVALID);

	return 0;
}

static int
native_format(void)
{
	const unsigned int rates[] = {60, 120, 240, 100, 144, 1, 0xffffffffU};
	unsigned int i;

	CHECK(mh_demo_header_size(MH_DEMO_VERSION_40) == 6713);
	CHECK(mh_demo_crossings_offset(MH_DEMO_VERSION_40) == 1097);

	for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
		if (native_rate_roundtrip(rates[i]) != 0) {
			fprintf(stderr, "native recording: simulation=%u Hz\n",
				rates[i]);
			return 1;
		}
	}

	CHECK(native_timing_rejections() == 0);

	return 0;
}

static int
patched_format(void)
{
	unsigned char data[6705 + 65 * 398], encoded[6706], saved_header[6706];
	struct mh_demo demo, saved_demo;
	struct mh_demo_frame frame;
	const unsigned char *body;
	size_t size, frame_index, length;
	unsigned int mask, i, count;

	CHECK(mh_demo_header_size(MH_DEMO_VERSION_25) == 4012);
	CHECK(mh_demo_header_size(MH_DEMO_VERSION_30) == 6705);
	CHECK(mh_demo_header_size((enum mh_demo_version)99) == 0);

	for (mask = 1; mask < 8192; mask++) {
		size = write_fixture(data, mask, 1);
		CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_NONE);
		CHECK(demo.version == MH_DEMO_VERSION_30);
		memset(encoded, 0xa5, sizeof(encoded));
		CHECK(mh_demo_header_write(&demo, encoded, sizeof(encoded),
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(encoded, data, 6705) == 0 &&
			encoded[6705] == 0xa5);
		count = 0;

		for (i = 0; i < 13; i++) {
			if (mask & (1U << i))
				count++;

			CHECK(demo.players[i].horn[15] == 'k' + (int)i &&
				demo.players[i].horn[16] == 0);
			CHECK(demo.players[i].colors[3] ==
				0xfedc0000UL + 100 * i + 3);
			CHECK(demo.players[i].crossings[26][3] ==
				0xf0000000UL + 1000 * i + 263);
		}

		CHECK(demo.player_count == count &&
			demo.frame_bytes == 8 + 30 * count);

		for (frame_index = 0; frame_index < 65; frame_index++) {
			CHECK(mh_demo_frame_read(&demo, frame_index, &frame,
				NULL) == MH_ERROR_NONE);
			CHECK(frame.active_mask ==
				((frame_index & 1) ? 0 : mask));
			CHECK(frame.unavailable_mask == mask &&
				(frame.flags >> 26) == 63);
			CHECK(frame.milliseconds ==
				(frame_index == 0
						? MH_U32_MAX
						: (unsigned long)frame_index));
			body = data + 6705 + frame_index * demo.frame_bytes + 8;

			for (i = 0; i < 13; i++) {
				if (mask & (1U << i)) {
					CHECK(frame.vehicles[i] == body &&
						body[0] == i + frame_index &&
						body[29] == i + frame_index);
					body += 30;
				} else
					CHECK(frame.vehicles[i] == NULL);
			}
		}
	}

	/* Short reads and failed writes must not expose partial output. */
	memcpy(&saved_demo, &demo, sizeof(saved_demo));
	memcpy(saved_header, encoded, sizeof(encoded));

	for (length = 0; length < size; length++) {
		if (length == size - demo.frame_bytes)
			continue;

		CHECK(mh_demo_read(data, length, &demo, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&saved_demo, &demo, sizeof(saved_demo)) == 0);
	}

	CHECK(mh_demo_header_write(&demo, encoded, 6704, NULL) !=
		MH_ERROR_NONE);
	demo.version = MH_DEMO_VERSION_25;
	CHECK(mh_demo_header_write(&demo, encoded, sizeof(encoded), NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(encoded, saved_header, sizeof(encoded)) == 0);

	/*
	 * A sparse high slot cannot be silently lost by downgrading the header.
	 */
	size = write_fixture(data, 4097, 1);
	CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_NONE);
	demo.version = MH_DEMO_VERSION_25;
	demo.local_player = 0;
	CHECK(mh_demo_header_write(&demo, encoded, sizeof(encoded), NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(encoded, saved_header, sizeof(encoded)) == 0);

	return 0;
}

int
main(void)
{
	unsigned char data[MH_DEMO_HEADER_BYTES + 65 * 248],
		encoded[MH_DEMO_HEADER_BYTES];
	struct mh_demo demo, saved_demo;
	struct mh_demo_frame frame, saved_frame;
	size_t size, length, frame_index;
	unsigned int mask, count, i, j, k;
	const unsigned char *cursor;

	for (mask = 1; mask < 256; mask++) {
		size = write_legacy_fixture(data, mask);
		count = 0;
		CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_NONE);
		CHECK(mh_demo_header_write(&demo, encoded, sizeof(encoded),
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(encoded, data, sizeof(encoded)) == 0);
		CHECK(demo.data == data && demo.size == size &&
			demo.frame_count == 65);
		CHECK(demo.flags == 0x87654321UL && demo.laps == 25);
		CHECK(strcmp(demo.track, "ABCDEFGHIJKLMNOP") == 0);

		for (i = 0; i < 8; i++) {
			if ((mask & (1U << i)) != 0)
				count++;

			CHECK(demo.players[i].active ==
				((mask & (1U << i)) != 0 ? i + 1 : 0));
			CHECK(demo.players[i].name[15] == 'a' + (int)i &&
				demo.players[i].name[16] == '\0');
			CHECK(demo.players[i].team[4] == 'A' + (int)i &&
				demo.players[i].team[5] == '\0');
			CHECK(demo.players[i].car[15] == '0' + (int)i &&
				demo.players[i].car[16] == '\0');

			for (j = 0; j < 4; j++)
				CHECK(demo.players[i].colors[j] ==
					0xfedc0000UL + 100 * i + j);

			CHECK(demo.players[i].gear_mode ==
				0xfedc0000UL + 100 * i + 4);
			CHECK(demo.players[i].control_type ==
				0xfedc0000UL + 100 * i + 5);

			for (j = 0; j < 27; j++)
				for (k = 0; k < 4; k++)
					CHECK(demo.players[i].crossings[j][k] ==
						0xf0000000UL + 1000 * i +
							10 * j + k);
		}

		CHECK(demo.player_count == count &&
			demo.frame_bytes == 8 + 30 * count);

		/*
		 * Embedded terminators do not erase otherwise meaningful raw
		 * tails.
		 */
		data[10] = data[36 + 4 + 3] = data[36 + 20 + 1] =
			data[36 + 25 + 4] = 0;
		CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_NONE);
		CHECK(mh_demo_header_write(&demo, encoded, sizeof(encoded),
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(encoded, data, sizeof(encoded)) == 0);

		for (frame_index = 0; frame_index < demo.frame_count;
			frame_index++) {
			CHECK(mh_demo_frame_read(&demo, frame_index, &frame,
				NULL) == MH_ERROR_NONE);
			CHECK(frame.active_mask ==
				((frame_index & 1) != 0 ? 0 : mask));
			CHECK(frame.unavailable_mask == mask &&
				(frame.flags >> 16) == 0xbeefUL);
			CHECK(frame.milliseconds ==
				(frame_index == 0
						? MH_U32_MAX
						: (unsigned long)frame_index));
			cursor = data + MH_DEMO_HEADER_BYTES +
				frame_index * demo.frame_bytes + 8;

			for (i = 0; i < 8; i++) {
				if ((mask & (1U << i)) != 0) {
					CHECK(frame.vehicles[i] == cursor &&
						cursor[0] == i + frame_index &&
						cursor[29] == i + frame_index);
					cursor += 30;
				} else
					CHECK(frame.vehicles[i] == NULL);
			}
		}
	}

	size = write_legacy_fixture(data, 255);
	CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_NONE);
	memcpy(&saved_demo, &demo, sizeof(saved_demo));

	for (length = 0; length < size; length++) {
		if (length == size - demo.frame_bytes)
			continue; /* Complete 64-frame input. */

		CHECK(mh_demo_read(data, length, &demo, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&demo, &saved_demo, sizeof(demo)) == 0);
	}

	CHECK(mh_demo_read(NULL, size, &demo, NULL) != MH_ERROR_NONE);
	CHECK(mh_demo_read(data, size, NULL, NULL) != MH_ERROR_NONE);
	write_word(data, 0x40200001UL);
	CHECK(mh_demo_read(data, size, &demo, NULL) != MH_ERROR_NONE);
	write_word(data, 0x40200000UL);
	write_word(data + 28, 0);
	CHECK(mh_demo_read(data, size, &demo, NULL) != MH_ERROR_NONE);
	write_word(data + 28, MH_U32_MAX);
	CHECK(mh_demo_read(data, size, &demo, NULL) != MH_ERROR_NONE);
	write_word(data + 28, 8);
	write_word(data + 32, 8);
	CHECK(mh_demo_read(data, size, &demo, NULL) != MH_ERROR_NONE);
	write_word(data + 32, 7);
	write_word(data + 36, 0);
	CHECK(mh_demo_read(data, size, &demo, NULL) != MH_ERROR_NONE);
	write_word(data + 36, 1);
	CHECK(memcmp(&demo, &saved_demo, sizeof(demo)) == 0);
	size = write_legacy_fixture(data, 1);
	write_word(data + 32, 1);
	CHECK(mh_demo_read(data, size, &demo, NULL) != MH_ERROR_NONE);
	write_word(data + 32, 0);
	CHECK(mh_demo_read(data, size, &demo, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_frame_read(&demo, 64, &frame, NULL) == MH_ERROR_NONE);
	memcpy(&saved_frame, &frame, sizeof(frame));
	CHECK(mh_demo_frame_read(&demo, 65, &frame, NULL) != MH_ERROR_NONE);
	CHECK(mh_demo_frame_read(&demo, (size_t)-1, &frame, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_frame_read(NULL, 0, &frame, NULL) != MH_ERROR_NONE);
	CHECK(mh_demo_frame_read(&demo, 0, NULL, NULL) != MH_ERROR_NONE);
	demo.frame_count = (size_t)-1;
	CHECK(mh_demo_frame_read(&demo, (size_t)-2, &frame, NULL) !=
		MH_ERROR_NONE);
	demo.size = MH_DEMO_HEADER_BYTES + 37;
	CHECK(mh_demo_frame_read(&demo, 0, &frame, NULL) != MH_ERROR_NONE);
	demo.size = size;
	demo.players[1].active = 1;
	CHECK(mh_demo_frame_read(&demo, 0, &frame, NULL) != MH_ERROR_NONE);
	demo.players[1].active = 0;
	demo.players[0].active = 0;
	CHECK(mh_demo_frame_read(&demo, 0, &frame, NULL) != MH_ERROR_NONE);
	demo.players[0].active = 1;
	demo.frame_bytes = 0;
	CHECK(mh_demo_frame_read(&demo, 0, &frame, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&frame, &saved_frame, sizeof(frame)) == 0);
	CHECK(patched_format() == 0);
	CHECK(native_format() == 0);
	puts("demo: MDE 3.0 all 8191 rosters, 13-slot masks, horn names and lossless headers passed");
	puts("demo: all 255 rosters, frame slot mapping, full header fields, truncation and atomic bounds passed");

	return 0;
}
