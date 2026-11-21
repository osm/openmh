#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/font/provider.h"
#include "backends/image/provider.h"
#include "backends/image/tga_provider.h"
#include "backends/render/software_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/assets/prepare_demo_results.h"
#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "engine/ui/results_assets.h"
#include "interfaces/render/backend.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "demo results line %d: %s\n",          \
				__LINE__, #expression);                        \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	FONT_TABLE_BYTES = 4 + 256 * 8,
	FONT_PROGRAM_BYTES = 8
};

static int
check_preparation(void)
{
	static const unsigned char glyph[FONT_PROGRAM_BYTES] = {
		0x88, 0x58, 0, 0xe9, 0, 0, 0, 0};

	unsigned char font[FONT_TABLE_BYTES + FONT_PROGRAM_BYTES];
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *source;
	const struct mh_user_data *data;
	struct mh_demo_recipe recipe;
	struct mh_file boards[2];
	struct mh_image image;
	unsigned int i;

	memset(&recipe, 0, sizeof(recipe));
	recipe.track_layout = MH_DEMO_TRACK_SOFTWARE_FIRST;
	memset(font, 0, sizeof(font));
	mh_write_u32le(font, 1);

	for (i = 0; i < 256; i++) {
		mh_write_u32le(font + 4 + i * 8, FONT_TABLE_BYTES);
		mh_write_u32le(font + 8 + i * 8, 1);
	}

	memcpy(font + FONT_TABLE_BYTES, glyph, sizeof(glyph));
	entries[0].path = "root/data/font1.fnt";
	entries[0].data = font;
	entries[0].size = sizeof(font);
	CHECK(mh_memory_resources_open(entries, 1, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_results_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "root/data/results.tga", 800000,
		&boards[0], NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "root/data/results2.tga", 800000,
		&boards[1], NULL) == MH_ERROR_NONE);

	for (i = 0; i < 2; i++) {
		CHECK(mh_image_tga_decode(boards[i].data, boards[i].size, 0,
			&image, NULL) == MH_ERROR_NONE);
		CHECK(image.width == 640 && image.height == 400);
		CHECK(image.rgba[0] == 10 && image.rgba[1] == 15 &&
			image.rgba[2] == 35);

		/*
		 * Rank one at x=24, y=85 uses coverage from the supplied font.
		 */
		CHECK(image.rgba[(85 * 640 + 24) * 4] == 108);

		/*
		 * The shorter board does not invent slots for absent players.
		 */
		CHECK(image.rgba[(185 * 640 + 30) * 4] == (i == 0 ? 22 : 10));
		free(image.rgba);
		free(boards[i].data);
	}

	mh_memory_resources_close(source);
	mh_memory_user_data_close(data);
	entries[1].path = "root/data/RESULTS2.TGA";
	entries[1].data = (const unsigned char *)"original";
	entries[1].size = 8;
	CHECK(mh_memory_resources_open(entries, 2, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_results_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_EXISTS);
	CHECK(mh_user_data_read(data, "root/data/results.tga", 800000,
		&boards[0], NULL) == MH_ERROR_NOT_FOUND);
	mh_memory_resources_close(source);

	/* MPlayer does not read fonts or replace any shipped presentation. */
	recipe.track_layout = MH_DEMO_TRACK_MPLAYER;
	CHECK(mh_memory_resources_open(NULL, 0, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_results_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "root/data/results.tga", 800000,
		&boards[0], NULL) == MH_ERROR_NOT_FOUND);
	mh_memory_resources_close(source);
	mh_memory_user_data_close(data);

	return 0;
}

static int
capture_board(const struct mh_ui_atlas *atlas, const struct mh_ui_list *list,
	const char *prefix, unsigned int count)
{
	struct mh_renderer *renderer;
	struct mh_file picture;
	FILE *file;
	char *path;
	int success;

	renderer = NULL;
	memset(&picture, 0, sizeof(picture));
	success = mh_renderer_open(mh_software_backend(), NULL, NULL, 0,
			list->width, list->height, 0, &renderer,
			NULL) == MH_ERROR_NONE &&
		mh_renderer_ui_upload(renderer, atlas) &&
		mh_renderer_ui_frame(renderer, list) &&
		mh_renderer_tga_capture(renderer, &picture);
	mh_renderer_destroy(renderer);

	if (success && prefix != NULL) {
		path = malloc(strlen(prefix) + 32);
		success = path != NULL;

		if (path != NULL) {
			sprintf(path, "%s-%u-%u.tga", prefix, count,
				list->width);
			file = fopen(path, "wb");
			success = file != NULL;

			if (file != NULL) {
				success = fwrite(picture.data, 1, picture.size,
						file) == picture.size;

				if (fclose(file) != 0)
					success = 0;
			}

			free(path);
		}
	}

	free(picture.data);

	return success;
}

static int
check_results(const char *root, const char *capture_prefix)
{
	static const char *names[8] = {"Driver", "Two", "Three", "Four", "Five",
		"Six", "Seven", "Eight"};

	static struct mh_ui_list list;
	struct mh_ui_atlas atlas;
	struct mh_results_assets assets;
	struct mh_standings standings;
	struct mh_error error;
	unsigned int count, i;
	enum mh_error_code status;

	for (count = 1; count <= 8; count *= 2) {
		CHECK(mh_ui_atlas_init(2048, 1024, &atlas, NULL) ==
			MH_ERROR_NONE);
		status = mh_results_assets_players_from(
			mh_font_fallback_source(), mh_image_fallback_decoder(),
			mh_image_tga_portrait_decoder(),
			mh_filesystem_resources(), root, count, names, &atlas,
			&assets, &error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "%s\n", error.message);

		CHECK(status == MH_ERROR_NONE);
		memset(&standings, 0, sizeof(standings));
		standings.count = count;

		for (i = 0; i < count; i++) {
			standings.order[i] = i;
			standings.cars[i].position = i + 1;
			standings.cars[i].finished = 1;
			standings.cars[i].finish_ms = 90000 + i * 1000;
		}

		CHECK(mh_results_draw(&assets, &atlas, &standings, 640, 400,
			"Press Enter", &list));
		CHECK(capture_board(&atlas, &list, capture_prefix, count));
		CHECK(mh_results_draw(&assets, &atlas, &standings, 960, 640,
			"Press Enter", &list));
		CHECK(capture_board(&atlas, &list, capture_prefix, count));
		free(atlas.image.rgba);
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc == 1)
		return check_preparation();

	if (argc == 2 || argc == 3)
		return check_results(argv[1], argc == 3 ? argv[2] : NULL);

	fprintf(stderr,
		"Usage: test-demo-results [PREPARED-GAME [CAPTURE-PREFIX]]\n");

	return 1;
}
