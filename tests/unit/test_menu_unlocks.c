#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_unlocks.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "menu unlock line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	unsigned long track_divisions[64], available[64], car_divisions[64];
	struct mh_menu_track_unlocks track_unlocks, saved_tracks;
	struct mh_menu_car_unlocks car_unlocks, saved_cars;
	unsigned int i;
	static const unsigned long installed_tracks[8] = {
		100, 101, 200, 201, 300, 301, 310, 400};

	static const unsigned long installed_cars[10] = {
		100, 101, 102, 200, 201, 202, 300, 302, 302, 410};

	memcpy(track_divisions, installed_tracks, sizeof(installed_tracks));
	memcpy(car_divisions, installed_cars, sizeof(installed_cars));
	memset(available, 0, sizeof(available));
	CHECK(mh_menu_track_unlocks(track_divisions, available, 8, 7, 99,
		&track_unlocks, NULL) == MH_ERROR_NONE);
	CHECK(track_unlocks.selected == 0 && track_unlocks.visible == 6 &&
		track_unlocks.unlocked == 2 && track_unlocks.record_class == 1);

	/* Original special-track decrement wraps even on fresh data. */
	CHECK(track_unlocks.completed[2] == MH_U32_MAX);
	CHECK(mh_menu_car_unlocks(car_divisions, 10, 9, track_unlocks.divisions,
		available, 8, track_unlocks.visible,
		track_unlocks.unlocked, &car_unlocks,
		NULL) == MH_ERROR_NONE);
	CHECK(car_unlocks.selected == 2 && car_unlocks.visible == 9 &&
		car_unlocks.unlocked == 3);

	for (i = 0; i < 8; i++)
		available[i] = 1;

	CHECK(mh_menu_track_unlocks(track_divisions, available, 8, 7, 99,
		&track_unlocks, NULL) == MH_ERROR_NONE);
	CHECK(track_unlocks.selected == 7 && track_unlocks.visible == 8 &&
		track_unlocks.unlocked == 8 &&
		track_unlocks.record_class == 99);
	CHECK(mh_menu_car_unlocks(car_divisions, 10, 9, track_unlocks.divisions,
		available, 8, 8, 8, &car_unlocks, NULL) == MH_ERROR_NONE);
	CHECK(car_unlocks.selected == 9 && car_unlocks.visible == 10 &&
		car_unlocks.unlocked == 10);
	available[7] = 0;
	CHECK(mh_menu_track_unlocks(track_divisions, available, 8, 7, 99,
		&track_unlocks, NULL) == MH_ERROR_NONE);
	CHECK(track_unlocks.selected == 6 && track_unlocks.visible == 7 &&
		track_unlocks.unlocked == 7 &&
		track_unlocks.record_class == 99);
	saved_tracks = track_unlocks;
	saved_cars = car_unlocks;
	track_divisions[0] = 500;
	CHECK(mh_menu_track_unlocks(track_divisions, available, 8, 0, 0,
		&track_unlocks, NULL) != MH_ERROR_NONE &&
		memcmp(&track_unlocks, &saved_tracks, sizeof(track_unlocks)) ==
			0);
	track_divisions[0] = 0;
	track_divisions[1] = 99;
	CHECK(mh_menu_track_unlocks(track_divisions, available, 8, 0, 0,
		&track_unlocks, NULL) == MH_ERROR_NONE &&
		track_unlocks.divisions[0] == 100 &&
		track_unlocks.divisions[1] == 100);
	saved_tracks = track_unlocks;

	for (i = 0; i < 17; i++)
		track_divisions[i] = 100;

	CHECK(mh_menu_track_unlocks(track_divisions, available, 17, 0, 0,
		&track_unlocks, NULL) != MH_ERROR_NONE &&
		memcmp(&track_unlocks, &saved_tracks, sizeof(track_unlocks)) ==
			0);
	car_divisions[0] = MH_U32_MAX;
	CHECK(mh_menu_car_unlocks(car_divisions, 10, 0, track_unlocks.divisions,
		available, 8, 7, 7, &car_unlocks,
		NULL) != MH_ERROR_NONE &&
		memcmp(&car_unlocks, &saved_cars, sizeof(car_unlocks)) == 0);

	/* Patched final special course needs no separate completion marker. */
	memcpy(track_divisions, installed_tracks, sizeof(installed_tracks));
	memcpy(car_divisions, installed_cars, sizeof(installed_cars));
	track_divisions[8] = 410;

	for (i = 0; i < 8; i++)
		available[i] = 1;

	available[8] = 0;
	CHECK(mh_menu_track_unlocks(track_divisions, available, 9, 8, 99,
		&track_unlocks, NULL) == MH_ERROR_NONE);
	CHECK(track_unlocks.selected == 8 && track_unlocks.visible == 9 &&
		track_unlocks.unlocked == 9 &&
		track_unlocks.record_class == 99);
	CHECK(mh_menu_car_unlocks(car_divisions, 10, 9, track_unlocks.divisions,
		available, 9, track_unlocks.visible,
		track_unlocks.unlocked, &car_unlocks,
		NULL) == MH_ERROR_NONE);
	CHECK(car_unlocks.selected == 9 && car_unlocks.visible == 10 &&
		car_unlocks.unlocked == 10);

	puts("menu unlocks: new/completed catalogs, special counters, normalization and atomic bounds passed");

	/*
	 * Extra courses are always selectable and never unlock stock content.
	 */
	memcpy(track_divisions + 2, installed_tracks, sizeof(installed_tracks));
	track_divisions[0] = 100002;
	track_divisions[1] = 100001;
	memset(available, 0, sizeof(available));
	available[0] = available[1] = 1;
	memcpy(car_divisions, installed_cars, sizeof(installed_cars));
	CHECK(mh_menu_track_unlocks(track_divisions, available, 10, 1, 1,
		&track_unlocks, NULL) == MH_ERROR_NONE);
	CHECK(track_unlocks.selected == 1 && track_unlocks.visible == 8 &&
		track_unlocks.unlocked == 4);
	CHECK(track_unlocks.total[0] == 2 && track_unlocks.completed[0] == 0 &&
		track_unlocks.indices[0][0] == 2);
	CHECK(mh_menu_car_unlocks(car_divisions, 10, 9, track_unlocks.divisions,
		available, 10, track_unlocks.visible,
		track_unlocks.unlocked, &car_unlocks,
		NULL) == MH_ERROR_NONE);
	CHECK(car_unlocks.visible == 9 && car_unlocks.unlocked == 3);

	return 0;
}
