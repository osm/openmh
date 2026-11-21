#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/race_records.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	unsigned char data[MH_RECORD_RACE_BYTES], saved[MH_RECORD_RACE_BYTES];
	struct mh_records records, updated, old;
	struct mh_race race;
	struct mh_record_notices notices, old_notices;
	struct mh_record_gaps gaps;
	struct mh_lap_record lap;
	struct mh_race_record whole;
	unsigned int i;

	for (i = 0; i < sizeof(data); i++)
		data[i] = (unsigned char)(i * 29U + 17);

	memcpy(saved, data, sizeof(data));
	CHECK(mh_lap_record_read(data, 36, &lap, NULL) == MH_ERROR_NONE);
	memset(data, 0, 36);
	CHECK(mh_lap_record_write(&lap, data, 36, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(data, saved, sizeof(data)) == 0);
	CHECK(mh_race_record_read(data, sizeof(data), &whole, NULL) ==
		MH_ERROR_NONE);
	memset(data, 0, sizeof(data));
	CHECK(mh_race_record_write(&whole, data, sizeof(data), NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(data, saved, sizeof(data)) == 0);

	for (i = 0; i < sizeof(data); i++)
		CHECK(mh_race_record_read(data, i, &whole, NULL) !=
			MH_ERROR_NONE);

	CHECK(mh_lap_record_read(data, 37, &lap, NULL) != MH_ERROR_NONE);
	CHECK(mh_race_record_write(&whole, data, sizeof(data) - 1, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(data, saved, sizeof(data)) == 0);
	memset(&records, 0, sizeof(records));
	memset(&race, 0, sizeof(race));
	race.lap = 2;
	race.best_lap = 1;
	race.last_ms = race.best_ms = 10000;
	race.now_ms = 10000;
	race.splits[1][2] = 3000;
	race.splits[1][3] = 6000;
	race.splits[2][0] = 10000;
	CHECK(mh_race_records_accept(&records, &race, 1, &updated, &notices,
		NULL) == MH_ERROR_NONE);
	CHECK(updated.has_lap && !updated.has_race && notices.lap == 2 &&
		notices.race == 0);
	CHECK(updated.lap.splits[2] == 3000 && updated.lap.splits[3] == 6000);
	records = updated;
	CHECK(mh_race_records_accept(&records, &race, 1, &updated, &notices,
		NULL) == MH_ERROR_NONE);
	CHECK(notices.lap == 1 &&
		memcmp(&records, &updated, sizeof(records)) == 0);
	race.finished = 1;
	race.now_ms = 22000;
	race.last_ms = 12000;
	CHECK(mh_race_records_accept(&records, &race, 1, &updated, &notices,
		NULL) == MH_ERROR_NONE);
	CHECK(notices.lap == 0 && notices.race == 2 &&
		updated.race.rows[2].end_ms == 22000);
	CHECK(updated.race.rows[1].end_ms == 10000 &&
		updated.race.rows[2].splits[0] == 10000);
	records = updated;
	race.split_notice = 1;
	CHECK(mh_race_record_gaps(&records, &race, &gaps, NULL) ==
		MH_ERROR_NONE);
	CHECK(gaps.personal_lap == 2000 && gaps.global_lap == 2000 &&
		gaps.global_race == 0);
	race.last_ms = 9000;
	race.now_ms = 19000;
	CHECK(mh_race_records_accept(&records, &race, 0, &updated, &notices,
		NULL) == MH_ERROR_NONE);
	CHECK(notices.lap == 1 && notices.race == 1 &&
		memcmp(&records, &updated, sizeof(records)) == 0);
	old = updated;
	old_notices = notices;
	race.best_lap = 27;
	CHECK(mh_race_records_accept(&records, &race, 1, &updated, &notices,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&old, &updated, sizeof(old)) == 0 &&
		memcmp(&old_notices, &notices, sizeof(notices)) == 0);
	race.best_lap = 0;
	race.lap = 1;
	race.finished = 0;
	CHECK(mh_race_records_accept(&records, &race, 1, &updated, &notices,
		NULL) != MH_ERROR_NONE);
#if ULONG_MAX > MH_U32_MAX
	whole.rows[26].splits[3] = MH_U32_MAX + 1UL;
	CHECK(mh_race_record_write(&whole, data, sizeof(data), NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(data, saved, sizeof(data)) == 0);
#endif
	puts("record payload preservation, bounds, strict improvement and signed comparisons passed");

	return 0;
}
