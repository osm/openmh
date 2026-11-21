#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/chf_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/adp_provider.h"
#include "backends/catalog/provider.h"
#include "backends/font/fnt_provider.h"
#include "backends/font/provider.h"
#include "backends/image/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/audio/sample_path.h"
#include "engine/image/load.h"
#include "engine/storage/asset_path.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/catalog/profile_source.h"
#include "interfaces/catalog/source.h"
#include "interfaces/font/source.h"
#include "interfaces/storage/resource.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "demo sources:%d: %s\n", __LINE__,     \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

/* Compare runtime decoding with previously prepared, installed resources. */
static int
check_catalogs(const char *root, const char *prepared)
{
	const struct mh_resource_source *resources;
	const struct mh_catalog_source *source;
	struct mh_car car, expected_car;
	struct mh_track track, expected_track;
	struct mh_profile_catalog profiles, expected_profiles;
	struct mh_horn_catalog horns, expected_horns;
	unsigned int count, expected_count, i;

	resources = mh_filesystem_resources();
	source = mh_catalog_fallback_source();
	CHECK(mh_car_catalog_count_from(
		source, resources, root, &count, NULL) == MH_ERROR_NONE);
	CHECK(mh_car_catalog_count_from(source, resources, prepared,
		&expected_count, NULL) == MH_ERROR_NONE);
	CHECK(count == expected_count && count == 3);

	for (i = 1; i <= count; i++) {
		CHECK(mh_car_config_load_from(source, resources, root, i, &car,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_car_config_load_from(source, resources, prepared, i,
			&expected_car, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&car, &expected_car, sizeof(car)) == 0);
	}

	CHECK(mh_track_catalog_count_from(
		source, resources, root, &count, NULL) == MH_ERROR_NONE);
	CHECK(mh_track_catalog_count_from(source, resources, prepared,
		&expected_count, NULL) == MH_ERROR_NONE);
	CHECK(count == expected_count && (count == 1 || count == 2));

	for (i = 1; i <= count; i++) {
		CHECK(mh_track_config_load_from(source, resources, root, i,
			&track, NULL) == MH_ERROR_NONE);
		CHECK(mh_track_config_load_from(source, resources, prepared, i,
			&expected_track, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&track, &expected_track, sizeof(track)) == 0);
	}

	CHECK(mh_profile_catalog_read_from(mh_profile_fallback_source(),
		resources, root, &profiles, NULL) == MH_ERROR_NONE);
	CHECK(mh_profile_catalog_read_from(mh_profile_adp_source(), resources,
		prepared, &expected_profiles, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&profiles, &expected_profiles, sizeof(profiles)) == 0);
	CHECK(mh_horn_catalog_load_from(mh_horn_fallback_source(), resources,
		root, &horns, NULL) == MH_ERROR_NONE);
	CHECK(mh_horn_catalog_load_from(mh_horn_chf_source(), resources,
		prepared, &expected_horns, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&horns, &expected_horns, sizeof(horns)) == 0);

	return 0;
}

static int
check_fonts(const char *root, const char *prepared)
{
	const struct mh_resource_source *resources;
	struct mh_font font, expected;
	struct mh_font_program program, expected_program;

	resources = mh_filesystem_resources();
	CHECK(mh_font_load(mh_font_fallback_source(), resources, root,
		"Data/FONT1S.FNT", &font, NULL) == MH_ERROR_NONE);
	CHECK(mh_font_load(mh_font_fnt_source(), resources, prepared,
		"Data/FONT1S.FNT", &expected, NULL) == MH_ERROR_NONE);
	CHECK(font.image.width == expected.image.width &&
		font.image.height == expected.image.height);
	CHECK(font.cell_width == expected.cell_width &&
		font.height == expected.height);
	CHECK(memcmp(font.advance, expected.advance, sizeof(font.advance)) ==
		0);
	CHECK(memcmp(font.image.rgba, expected.image.rgba,
		(size_t)font.image.width * font.image.height * 4) == 0);
	free(font.image.rgba);
	free(expected.image.rgba);
	CHECK(mh_font_load_program(mh_font_fallback_source(), resources, root,
		"Data/FONT1S.FNT", &program, NULL) == MH_ERROR_NONE);
	CHECK(mh_font_load_program(mh_font_fnt_source(), resources, prepared,
		"Data/FONT1S.FNT", &expected_program,
		NULL) == MH_ERROR_NONE);
	CHECK(program.width == expected_program.width &&
		program.rows == expected_program.rows);
	CHECK(program.cell_width == expected_program.cell_width &&
		program.height == expected_program.height);
	CHECK(memcmp(program.advance, expected_program.advance,
		sizeof(program.advance)) == 0);
	CHECK(memcmp(program.bytes, expected_program.bytes,
		(size_t)program.width * program.rows) == 0);
	free(program.bytes);
	free(expected_program.bytes);

	return 0;
}

static int
check_presentation(const char *root, const char *prepared)
{
	static const char *const images[] = {"Data/results.tga",
		"Data/results2.tga", "Data/Loading.TGA", "Data/LoadAnim.tga"};

	static const char *const samples[] = {
		"Sounds/barrel.wav", "Sounds/crash2.wav"};

	const struct mh_resource_source *resources;
	struct mh_file actual, expected;
	char path[2048];
	unsigned int i;

	resources = mh_filesystem_resources();

	for (i = 0; i < sizeof(images) / sizeof(images[0]); i++) {
		CHECK(mh_image_read_from(mh_image_fallback_decoder(), resources,
			root, images[i], &actual, NULL) == MH_ERROR_NONE);
		CHECK(mh_image_file_read(mh_image_fallback_decoder(), resources,
			prepared, images[i], &expected,
			NULL) == MH_ERROR_NONE);
		CHECK(actual.size == expected.size &&
			memcmp(actual.data, expected.data, actual.size) == 0);
		free(actual.data);
		free(expected.data);
	}

	for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
		CHECK(mh_audio_sample_path_from(mh_audio_fallback_decoder(),
			resources, root, samples[i], path, sizeof(path),
			NULL) == MH_ERROR_NONE);
		CHECK(mh_resource_source_read(resources, path, &actual, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_asset_path_from(resources, prepared, samples[i], path,
			sizeof(path), NULL) == MH_ERROR_NONE);
		CHECK(mh_resource_source_read(resources, path, &expected,
			NULL) == MH_ERROR_NONE);
		CHECK(actual.size == expected.size &&
			memcmp(actual.data, expected.data, actual.size) == 0);
		free(actual.data);
		free(expected.data);
	}

	return 0;
}

static int
check_supplied_errors(const char *root)
{
	static const char *const names[] = {"memory/Game/Car1.car",
		"memory/Data/FONT1S.FNT", "memory/Data/results.tga",
		"memory/Sounds/barrel.wav"};

	static const unsigned char broken[] = {1, 2, 3};
	const struct mh_resource_source *resources;
	struct mh_resource_entry entries[2];
	struct mh_file executable;
	struct mh_car car, saved;
	struct mh_font font;
	struct mh_image image;
	char path[2048];
	unsigned int i;

	CHECK(mh_asset_path_from(mh_filesystem_resources(), root, "motor.exe",
		path, sizeof(path), NULL) == MH_ERROR_NONE);
	CHECK(mh_resource_source_read(mh_filesystem_resources(), path,
		&executable, NULL) == MH_ERROR_NONE);
	entries[0].path = "memory/motor.exe";
	entries[0].data = executable.data;
	entries[0].size = executable.size;
	entries[1].data = broken;
	entries[1].size = sizeof(broken);
	memset(&car, 0x37, sizeof(car));
	saved = car;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		entries[1].path = names[i];
		CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
			MH_ERROR_NONE);

		if (i == 0) {
			CHECK(mh_car_config_load_from(
				mh_catalog_fallback_source(), resources,
				"memory", 1, &car,
				NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&car, &saved, sizeof(car)) == 0);
		} else if (i == 1) {
			CHECK(mh_font_load(mh_font_fallback_source(), resources,
				"memory", "Data/FONT1S.FNT", &font,
				NULL) != MH_ERROR_NONE);
		} else if (i == 2) {
			CHECK(mh_image_load_from(mh_image_fallback_decoder(),
				resources, "memory", "Data/results.tga",
				NULL, &image, NULL,
				NULL) != MH_ERROR_NONE);
		} else {
			CHECK(mh_audio_sample_path_from(
				mh_audio_fallback_decoder(), resources,
				"memory", "Sounds/barrel.wav", path,
				sizeof(path), NULL) == MH_ERROR_NONE);
			CHECK(strcmp(path, names[i]) == 0);
		}

		mh_memory_resources_close(resources);
	}

	free(executable.data);

	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s ORIGINAL_INSTALL PREPARED_INSTALL\n",
			argv[0]);
		return 2;
	}

	if (check_catalogs(argv[1], argv[2]) || check_fonts(argv[1], argv[2]) ||
		check_presentation(argv[1], argv[2]) ||
		check_supplied_errors(argv[1]))
		return 1;

	puts("Original demo providers match prepared catalogs, defaults, fonts, artwork and samples; malformed supplied assets remain errors");

	return 0;
}
