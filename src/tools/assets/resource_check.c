#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/camera_motion.h"
#include "engine/assets/car.h"
#include "engine/assets/material.h"
#include "engine/assets/menu_layout.h"
#include "engine/assets/palette.h"
#include "engine/assets/profile.h"
#include "engine/assets/remap.h"
#include "engine/assets/retro.h"
#include "engine/assets/route.h"
#include "engine/assets/scene.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track.h"
#include "engine/core/error.h"
#include "engine/image/atlas_map.h"
#include "engine/settings/control_layout.h"
#include "engine/settings/menu_sound.h"
#include "engine/ui/menu.h"

static enum mh_error_code
check_control(const unsigned char *data, size_t size, int dump,
	struct mh_error *error_out)
{
	struct mh_control_layout layout;
	unsigned int i;
	enum mh_error_code status;

	status = mh_control_layout_read(data, size, NULL, &layout, error_out);

	if (status == MH_ERROR_NONE && dump) {
		printf("%s %.9g %.9g", layout.name,
			(double)layout.mouse_speed[0],
			(double)layout.mouse_speed[1]);

		for (i = 0; i < MH_CONTROL_BINDINGS; i++)
			printf(" %lu", layout.bindings[i]);

		putchar('\n');
	}

	return status;
}

static enum mh_error_code
check_menu_sound(const unsigned char *data, size_t size, int dump,
	struct mh_error *error_out)
{
	struct mh_menu_sound_config config;
	unsigned int i;
	enum mh_error_code status;

	status = mh_menu_sound_read(data, size, &config, error_out);

	if (status == MH_ERROR_NONE && dump) {
		printf("%u\n", config.count);

		for (i = 0; i < config.count; i++)
			printf("%s %.9g %.9g\n", config.samples[i].name,
				(double)config.samples[i].volume,
				(double)config.samples[i].pitch);

		for (i = 0; i < MH_MENU_SOUND_EVENTS; i++)
			printf("%u %u %d\n", config.events[i],
				mh_menu_sound_event(&config, i),
				mh_menu_sound_flash(i));
	}

	return status;
}

static enum mh_error_code
check_remap(const unsigned char *data, size_t size, int dump,
	struct mh_error *error_out)
{
	struct mh_atlas_map remap;
	struct mh_atlas_map_entry entry;
	unsigned long i;
	unsigned int j;
	enum mh_error_code status;

	status = mh_atlas_remap_decode(data, size, &remap, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!dump) {
		mh_atlas_map_free(&remap);
		return status;
	}

	printf("%lu %lu %lu %lu %lu %.9g %.9g\n", remap.count, remap.pages,
		remap.cutout_pages, remap.width, remap.height,
		(double)remap.scale[0], (double)remap.scale[1]);

	for (i = 0; i < remap.count; i++) {
		status = mh_atlas_map_entry(&remap, i, &entry, error_out);

		if (status != MH_ERROR_NONE) {
			mh_atlas_map_free(&remap);
			return status;
		}

		for (j = 0; j < sizeof(entry.name); j++)
			printf("%02x",
				(unsigned int)(unsigned char)entry.name[j]);

		printf(" %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", entry.page,
			entry.saved_handle, entry.x, entry.y, entry.transpose,
			entry.source_width, entry.source_height,
			entry.scaled_width, entry.scaled_height);
	}

	mh_atlas_map_free(&remap);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_palette(const unsigned char *data, size_t size, int dump,
	struct mh_error *error_out)
{
	unsigned long palette[MH_PALETTE_COLORS];
	unsigned int i;
	enum mh_error_code status;

	status = mh_palette_act_decode(data, size, palette, error_out);

	if (status == MH_ERROR_NONE && dump)
		for (i = 0; i < MH_PALETTE_COLORS; i++)
			printf("%06lx\n", palette[i]);

	return status;
}

static enum mh_error_code
check_resource(const char *kind, const unsigned char *data, size_t size,
	int dump, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (strcmp(kind, "control") == 0)
		return check_control(data, size, dump, error_out);

	if (strcmp(kind, "menu-sound") == 0)
		return check_menu_sound(data, size, dump, error_out);

	if (strcmp(kind, "car") == 0) {
		struct mh_car car;
		return mh_car_config_car_decode(data, size, &car, error_out);
	}

	if (strcmp(kind, "track") == 0) {
		struct mh_track track;
		return mh_track_config_trk_decode(
			data, size, &track, error_out);
	}

	if (strcmp(kind, "motion") == 0) {
		struct mh_scene_object motion;

		status = mh_motion_mot_parse(data, size, &motion, error_out);

		if (status == MH_ERROR_NONE)
			free(motion.keys);

		return status;
	}

	if (strcmp(kind, "route") == 0) {
		struct mh_route route;

		status = mh_route_mot_parse(data, size, &route, error_out);

		if (status == MH_ERROR_NONE)
			mh_route_free(&route);

		return status;
	}

	if (strcmp(kind, "camera") == 0) {
		struct mh_camera_motion motion;

		status = mh_camera_mot_parse(data, size, &motion, error_out);

		if (status == MH_ERROR_NONE)
			mh_camera_motion_free(&motion);

		return status;
	}

	if (strcmp(kind, "material") == 0 || strcmp(kind, "surface") == 0) {
		struct mh_materials materials;
		return mh_materials_mat_srf_decode_list(data, size,
			strcmp(kind, "surface") == 0, &materials, error_out);
	}

	if (strcmp(kind, "layout") == 0) {
		struct mh_menu_layout layout;
		return mh_menu_layout_dta_decode(
			data, size, &layout, error_out);
	}

	if (strncmp(kind, "retro-", 6) == 0) {
		static const char *const names[] = {
			"meshes", "animation", "palette", "story"};

		unsigned int i;

		for (i = 0; i < MH_RETRO_PART_COUNT; i++)
			if (strcmp(kind + 6, names[i]) == 0)
				return mh_retro_mhretro_check(
					(enum mh_retro_part)i, data, size,
					error_out);
	}

	if (strcmp(kind, "profile") == 0) {
		struct mh_profile profile;
		return mh_profile_adp_decode(data, size, &profile, error_out);
	}

	if (strcmp(kind, "remap") == 0)
		return check_remap(data, size, dump, error_out);

	if (strcmp(kind, "palette") == 0)
		return check_palette(data, size, dump, error_out);

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "unknown resource kind");
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	unsigned char *decoded;
	const unsigned char *data;
	size_t size;
	struct mh_error error;
	enum mh_error_code status;
	int first, dump;

	dump = argc == 4 && strcmp(argv[1], "--dump") == 0;
	first = dump ? 2 : 1;

	if (argc != first + 2) {
		fprintf(stderr,
			"Usage: mh-resource-check [--dump] car|track|motion|route|camera|material|surface|layout|retro-meshes|retro-animation|retro-palette|retro-story|profile|remap|palette|control|menu-sound FILE\n");
		return 2;
	}

	if (dump && strcmp(argv[first], "remap") != 0 &&
		strcmp(argv[first], "palette") != 0 &&
		strcmp(argv[first], "control") != 0 &&
		strcmp(argv[first], "menu-sound") != 0) {
		fprintf(stderr,
			"Only remap, palette, control and menu-sound support --dump\n");
		return 2;
	}

	status = mh_file_read(argv[first + 1], &file, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Resource: %s\n", error.message);
		return 1;
	}

	data = file.data;
	size = file.size;
	decoded = NULL;

	if (size >= 3 && memcmp(data, "TBL", 3) == 0) {
		status = mh_tbl_resource_decode(
			data, size, NULL, &decoded, &size, &error);

		if (status == MH_ERROR_NONE)
			data = decoded;
	}

	if (status == MH_ERROR_NONE)
		status = check_resource(argv[first], data, size, dump, &error);

	free(decoded);
	free(file.data);

	if (status == MH_ERROR_NONE && ferror(stdout))
		status = mh_error_set(
			&error, MH_ERROR_IO, "resource output failed");

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Resource: %s\n", error.message);

	return status != MH_ERROR_NONE;
}
