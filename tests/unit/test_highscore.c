#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/highscore.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "high scores line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	TABLE_BYTES = 560,
	FILENAME_BYTES = 272,
	TEXT_BYTES = 256
};

int
main(void)
{
	struct mh_highscore_table table, before, decoded;
	struct mh_highscore_query query;
	struct mh_highscore_candidate candidate;
	unsigned char bytes[TABLE_BYTES], original[TABLE_BYTES];
	char name[FILENAME_BYTES], saved_name[FILENAME_BYTES],
		long_name[TEXT_BYTES];
	unsigned int i, rank;

	for (i = 0; i < sizeof(bytes); i++)
		bytes[i] = (unsigned char)(i * 37 + 11);

	memcpy(original, bytes, sizeof(bytes));
	CHECK(mh_highscore_read(bytes, sizeof(bytes), &table, NULL) ==
			MH_ERROR_NONE &&
		table.dirty == 0);
	CHECK(mh_highscore_write(&table, bytes, sizeof(bytes), NULL) ==
			MH_ERROR_NONE &&
		memcmp(bytes, original, sizeof(bytes)) == 0);
	before = table;

	for (i = 0; i < sizeof(bytes); i++) {
		CHECK(mh_highscore_read(bytes, i, &table, NULL) !=
				MH_ERROR_NONE &&
			memcmp(&before, &table, sizeof(table)) == 0);
		CHECK(mh_highscore_write(&table, bytes, i, NULL) !=
				MH_ERROR_NONE &&
			memcmp(bytes, original, sizeof(bytes)) == 0);
	}

	CHECK(mh_highscore_defaults(MH_HIGHSCORE_LAP, 0, &table, NULL) ==
		MH_ERROR_NONE);
	CHECK(table.rows[0].lap_ms == 240000 &&
		table.rows[9].lap_ms == 249000 && table.rows[9].race_ms == 0);
	CHECK(memcmp(table.rows[9].name, "DI dude 10", 11) == 0);
	CHECK(mh_highscore_rank(&table, 1, 240000, &rank, NULL) ==
			MH_ERROR_NONE &&
		rank == 2);
	CHECK(mh_highscore_rank(&table, 0, 0, &rank, NULL) == MH_ERROR_NONE &&
		rank == 0);
	CHECK(mh_highscore_rank(&table, 1, MH_U32_MAX, &rank, NULL) ==
			MH_ERROR_NONE &&
		rank == 0);
	rank = 42;
	CHECK(mh_highscore_rank(&table, 2, 0, &rank, NULL) != MH_ERROR_NONE &&
		rank == 42);
	CHECK(mh_highscore_defaults(MH_HIGHSCORE_RACE, 25, &table, NULL) ==
			MH_ERROR_NONE &&
		table.rows[9].race_ms == 6225000);
	before = table;
	CHECK(mh_highscore_defaults(MH_HIGHSCORE_RACE, 0, &table, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&before, &table, sizeof(table)) == 0);
	CHECK(mh_highscore_defaults(MH_HIGHSCORE_LEAGUE, 0, &table, NULL) ==
			MH_ERROR_NONE &&
		table.rows[9].race_ms == 24900000);
	CHECK(mh_highscore_overlay(MH_HIGHSCORE_LAP, 0, original, 3, &table,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_highscore_write(&table, bytes, sizeof(bytes), NULL) ==
			MH_ERROR_NONE &&
		memcmp(bytes, original, 3) == 0 && bytes[3] == 0);

	memset(&query, 0, sizeof(query));
	query.track = "RedRock";
	CHECK(mh_highscore_filename(&query, name, sizeof(name), NULL) ==
			MH_ERROR_NONE &&
		strcmp(name, "BLMN00RedRock.MHI") == 0);
	query.kind = MH_HIGHSCORE_SINGLE;
	query.laps = 25;
	query.difficulty = 2;
	query.record_class = 1;
	CHECK(mh_highscore_filename(&query, name, sizeof(name), NULL) ==
			MH_ERROR_NONE &&
		strcmp(name, "SRHM25RedRock.MHI") == 0);
	memcpy(saved_name, name, sizeof(name));
	CHECK(mh_highscore_filename(&query, name, strlen(name), NULL) !=
			MH_ERROR_NONE &&
		memcmp(saved_name, name, sizeof(name)) == 0);
	query.track = "../RedRock";
	CHECK(mh_highscore_filename(&query, name, sizeof(name), NULL) !=
			MH_ERROR_NONE &&
		memcmp(saved_name, name, sizeof(name)) == 0);
	query.kind = MH_HIGHSCORE_LEAGUE;
	query.division = 2;
	query.track = NULL;
	CHECK(mh_highscore_filename(&query, name, sizeof(name), NULL) ==
			MH_ERROR_NONE &&
		strcmp(name, "LH1.MHI") == 0);
	query.division = 3;
	CHECK(mh_highscore_filename(&query, name, sizeof(name), NULL) !=
		MH_ERROR_NONE);

	CHECK(mh_highscore_defaults(MH_HIGHSCORE_LAP, 0, &table, NULL) ==
		MH_ERROR_NONE);
	before = table;
	memset(long_name, 'X', sizeof(long_name));
	long_name[255] = 0;
	candidate.name = long_name;
	candidate.name_size = sizeof(long_name);
	candidate.car = "\305\344\366";
	candidate.car_size = 4;
	candidate.transmission = 1;
	candidate.auxiliary = 123;
	candidate.lap_ms = 0;
	candidate.race_ms = MH_U32_MAX;
	CHECK(mh_highscore_insert(&table, 2, &candidate, NULL) ==
			MH_ERROR_NONE &&
		table.dirty == 1);
	CHECK(memcmp(&table.rows[0], &before.rows[0], sizeof(table.rows[0])) ==
		0);
	CHECK(memcmp(&table.rows[2], &before.rows[1], sizeof(table.rows[2])) ==
		0);
	CHECK(memcmp(&table.rows[9], &before.rows[8], sizeof(table.rows[9])) ==
		0);
	CHECK(table.rows[1].name[22] == 'X' && table.rows[1].name[23] == 0);
	CHECK(memcmp(table.rows[1].car, "\305\344\366", 4) == 0 &&
		table.rows[1].car[4] == 'n');
	CHECK(table.rows[1].lap_ms == 0 &&
		table.rows[1].race_ms == MH_U32_MAX &&
		table.rows[1].auxiliary == 123);
	before = table;

	/* NUL is just outside the advertised bound. */
	candidate.name_size = 255;
	CHECK(mh_highscore_set(&table, 1, &candidate, NULL) != MH_ERROR_NONE &&
		memcmp(&table, &before, sizeof(table)) == 0);
	CHECK(mh_highscore_insert(&table, 1, &candidate, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&table, &before, sizeof(table)) == 0);
	CHECK(mh_highscore_insert(&table, 0, NULL, NULL) == MH_ERROR_NONE &&
		mh_highscore_set(&table, 11, NULL, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&table, &before, sizeof(table)) == 0);
	candidate.name_size = 256;
	candidate.name = "";
	candidate.car = "";
	CHECK(mh_highscore_set(&table, 10, &candidate, NULL) == MH_ERROR_NONE &&
		table.rows[9].name[0] == 0 && table.rows[9].name[1] == 'I');
	CHECK(mh_highscore_write(&table, bytes, sizeof(bytes), NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_read(bytes, sizeof(bytes), &decoded, NULL) ==
			MH_ERROR_NONE &&
		decoded.dirty == 0);
#if ULONG_MAX > MH_U32_MAX
	memcpy(original, bytes, sizeof(bytes));
	table.rows[9].race_ms = (MH_U32_MAX + 1UL);
	CHECK(mh_highscore_write(&table, bytes, sizeof(bytes), NULL) !=
			MH_ERROR_NONE &&
		memcmp(bytes, original, sizeof(bytes)) == 0);
	rank = 42;
	CHECK(mh_highscore_rank(&table, 0, 0, &rank, NULL) != MH_ERROR_NONE &&
		rank == 42);
#endif
	puts("High-score codecs, filenames, defaults, ties, bounded text, shifted rows and atomic errors passed");

	return 0;
}
