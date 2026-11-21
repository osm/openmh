#include <stdio.h>
#include <string.h>

#include "backends/network/udp_description.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

/* Independent layout: type-5 control header followed by a type-6 roster. */
static void
description_bytes(unsigned char *data, unsigned int count)
{
	unsigned int i;
	unsigned char *profile;

	memset(data, 0, 1123);
	data[0] = 5;
	data[2] = 0x63;
	data[3] = 4;
	memcpy(data + 4, "Original host", 13);
	data[20] = 6;
	data[22] = 0x4f;
	data[23] = 4;
	memcpy(data + 24, "Fixture Track", 13);
	data[40] = 2;
	data[41] = 5;
	data[42] = (unsigned char)count;
	data[43] = 10;

	for (i = 0; i < count; i++) {
		profile = data + 44 + 83 * i;
		profile[0] = (unsigned char)(i == 0 ? 2 : i == 1 ? 1 : 0);
		profile[1] = 20;
		memcpy(profile + 2, "Driver", 6);
		memcpy(profile + 18, "Team", 4);
		memcpy(profile + 24, "Car", 3);
		memcpy(profile + 40, "Horn", 4);
	}

	/*
	 * Original copies all 13 slots, including uninterpreted inactive bytes.
	 */
	memset(data + 44 + 83 * count, 0xa5, 83 * (13 - count));
}

static void
descriptions(void)
{
	static const unsigned int counts[] = {8, 13};
	struct mh_discovery_advertisement game;
	struct mh_discovery_catalog catalog;
	unsigned char data[1123], encoded[1123];
	unsigned int i;
	size_t size;

	for (i = 0; i < 2; i++) {
		description_bytes(data, counts[i]);
		CHECK(mh_udp_description_decode(data, sizeof(data), &game,
			NULL) == MH_ERROR_NONE);
		CHECK(strcmp(game.name, "Original host") == 0);
		CHECK(strcmp(game.track, "Fixture Track") == 0);
		CHECK(game.has_roster == 1 &&
			game.roster.player_count == counts[i]);
		CHECK(game.players == 2 && game.racing == 1);
		CHECK(game.mode == 2 && game.laps == 5 && game.car_count == 10);
		CHECK(game.password == MH_DISCOVERY_PASSWORD_UNKNOWN);
		CHECK(strcmp(game.roster.players[1].name, "Driver") == 0);
		CHECK(mh_udp_description_encode(&game, encoded, sizeof(encoded),
			&size, NULL) == MH_ERROR_NONE);
		CHECK(size == sizeof(data) && memcmp(data, encoded, size) == 0);
		memset(&catalog, 0, sizeof(catalog));
		CHECK(mh_discovery_collect(&catalog, 0x7f000001UL, 3010, &game,
			NULL) == MH_ERROR_NONE);
		CHECK(catalog.count == 1 &&
			catalog.entries[0].game.has_roster == 1);
		CHECK(catalog.entries[0].game.roster.player_count == counts[i]);
	}
}

static void
invalid_descriptions(void)
{
	static const unsigned char query[] = {4, 0, 4, 0};
	struct mh_discovery_advertisement game, saved;
	unsigned char data[1123], output[1123], previous[1123];
	size_t size;

	description_bytes(data, 8);
	CHECK(mh_udp_description_decode(data, sizeof(data), &game, NULL) ==
		MH_ERROR_NONE);
	saved = game;
	CHECK(mh_udp_description_decode(data, sizeof(data) - 1, &game, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&game, &saved, sizeof(game)) == 0);
	data[20] = 7;
	CHECK(mh_udp_description_decode(data, sizeof(data), &game, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&game, &saved, sizeof(game)) == 0);
	data[20] = 6;
	data[42] = 14;
	CHECK(mh_udp_description_decode(data, sizeof(data), &game, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&game, &saved, sizeof(game)) == 0);
	CHECK(mh_udp_description_decode(query, sizeof(query), &game, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&game, &saved, sizeof(game)) == 0);
	memset(output, 0x59, sizeof(output));
	memcpy(previous, output, sizeof(output));
	size = 123;
	CHECK(mh_udp_description_encode(&game, output, sizeof(output) - 1,
		&size, NULL) != MH_ERROR_NONE);
	CHECK(size == 123 && memcmp(previous, output, sizeof(output)) == 0);
	game.has_roster = 0;
	CHECK(mh_udp_description_encode(&game, output, sizeof(output), &size,
		NULL) != MH_ERROR_NONE);
	CHECK(size == 123 && memcmp(previous, output, sizeof(output)) == 0);
}

int
main(void)
{
	descriptions();
	invalid_descriptions();

	return failures != 0;
}
