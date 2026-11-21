#include <limits.h>
#include <stdio.h>

#include "client/menus/internal.h"
#include "engine/core/error.h"
#include "interfaces/storage/resource.h"

enum mh_error_code
mh_menus_unlocks_apply(struct mh_menus *menus,
	const unsigned long divisions[MH_TRACK_CATALOG_LIMIT],
	const unsigned long flags[MH_TRACK_CATALOG_LIMIT],
	struct mh_error *error_out)
{
	struct mh_menu_track_unlocks tracks;
	struct mh_menu_car_unlocks cars;
	unsigned long available[MH_TRACK_CATALOG_LIMIT];
	unsigned int i;
	enum mh_error_code status;

	if (mh_menus_compact(menus)) {
		mh_menus_compact_choices(menus);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < menus->track_catalog.count; i++)
		available[i] = menus->unlock_cheat ? 1 : flags[i];

	status = mh_menu_track_unlocks(divisions, available,
		menus->track_catalog.count, menus->details.track, 1, &tracks,
		error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_car_unlocks(menus->car_divisions,
			menus->car_catalog.count, menus->single.car,
			tracks.divisions, available, menus->track_catalog.count,
			tracks.visible, tracks.unlocked, &cars, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	menus->track_unlocks = tracks;
	menus->car_unlocks = cars;
	menus->availability = 0;

	for (i = 0; i < menus->track_catalog.count; i++) {
		menus->tracks[i].division = tracks.divisions[i];

		if (available[i] && i < sizeof(menus->availability) * CHAR_BIT)
			menus->availability |= 1U << i;
	}

	menus->race_scene.count = menus->track_catalog.count;

	if (!menus->track_override) {
		menus->details.track = tracks.selected;
		menus->race_scene.count = tracks.visible;
		menus->race_scene.unlocked = tracks.unlocked;
	}

	menus->car_scene.catalog.visible = menus->car_catalog.count;

	if (!menus->car_override) {
		menus->single.car = cars.selected;
		menus->car_scene.catalog.visible = cars.visible;
		menus->car_scene.catalog.unlocked = cars.unlocked;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menus_unlocks_refresh(struct mh_menus *menus, struct mh_error *error_out)
{
	const struct mh_track *track;
	unsigned long divisions[MH_TRACK_CATALOG_LIMIT],
		available[MH_TRACK_CATALOG_LIMIT];
	unsigned int i;
	enum mh_error_code status;

	for (i = 0; i < menus->track_catalog.count; i++) {
		track = &menus->track_catalog
				.entries[menus->track_numbers[i] - 1];
		status = mh_progress_available_from(menus->resources,
			menus->user_data, menus->asset_root,
			menus->progress_directory, track, &available[i],
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		divisions[i] = track->division;
	}

	status = mh_menus_unlocks_apply(menus, divisions, available, error_out);

	if (status == MH_ERROR_NONE) {
		printf("Progress: availability %02x, tracks %u/%u, cars %u/%u\n",
			menus->availability, menus->track_unlocks.unlocked,
			menus->track_unlocks.visible,
			menus->car_unlocks.unlocked,
			menus->car_unlocks.visible);
		fflush(stdout);
	}

	return status;
}

int
mh_menus_special_cars_available(
	const struct mh_resource_source *resources, const char *disc_root)
{
	return mh_progress_special_cars_available(resources, disc_root);
}
