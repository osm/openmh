#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_car_draw.h"
#include "engine/ui/menu_controls_draw.h"
#include "engine/ui/menu_credits.h"
#include "engine/ui/menu_ghost_draw.h"
#include "engine/ui/menu_graphics_draw.h"
#include "engine/ui/menu_league_draw.h"
#include "engine/ui/menu_main.h"
#include "engine/ui/menu_multi_draw.h"
#include "engine/ui/menu_options.h"
#include "engine/ui/menu_personal_draw.h"
#include "engine/ui/menu_race_draw.h"
#include "engine/ui/menu_rankings_draw.h"
#include "engine/ui/menu_single_draw.h"
#include "engine/ui/menu_sound_draw.h"
#include "interfaces/sprite/collection_backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "menu sprites:%d: %s\n", __LINE__,     \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	opens, closes, frames, palette_count;
	unsigned int	dots_frames, dots_width, dots_height;
	int		patched, fail_info, fail_frame;
	int		fail_second_frame;
};

static enum mh_error_code
open_collection(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, enum mh_picture_format format,
	void **output, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_file file;
	enum mh_error_code status;

	fixture = context;

	if (strcmp(root, "converted") != 0 ||
		strcmp(reference, "Data/MENU.SPR") != 0 ||
		format != MH_PICTURE_INDEXED)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"wrong menu collection request");

	status = mh_resource_source_read(
		resources, "converted/menu.frames", &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (file.size != 1 || file.data[0] != 73) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"wrong replacement frame resource");
	}

	*output = file.data;
	fixture->opens++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sprite_info(void *context, void *state, const char *name,
	struct mh_sprite_info *output, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)state;
	fixture = context;

	if (fixture->fail_info && strcmp(name, "main") == 0)
		return mh_error_set(
			error_out, MH_ERROR_IO, "metadata read failed");

	if (strcmp(name, "msel") == 0 ||
		(!fixture->patched &&
			(strcmp(name, "tbr") == 0 ||
				strcmp(name, "tmspx") == 0)))
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"optional sprite absent");

	output->width = output->height = 2;
	output->frames = strcmp(name, "dots") == 0 ? 2 : 1;

	if (strcmp(name, "dots") == 0) {
		output->frames = fixture->dots_frames;
		output->width = fixture->dots_width;
		output->height = fixture->dots_height;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sprite_palette(void *context, void *state, struct mh_image_palette *output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)state;
	fixture = context;
	memset(output, 0, sizeof(*output));
	output->count = fixture->palette_count;

	/* Duplicate colors are intentional; their indices must survive. */
	output->rgb[0] = output->rgb[3] = 73;
	output->rgb[767] = 119;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sprite_indexed(void *context, void *state, const char *name, unsigned int frame,
	struct mh_sprite_indexed *output, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_sprite_indexed image;
	unsigned char *color;

	fixture = context;
	color = state;

	if (fixture->fail_frame && strcmp(name, "main") == 0)
		return mh_error_set(
			error_out, MH_ERROR_IO, "frame read failed");

	if (fixture->fail_second_frame && frame == 1)
		return mh_error_set(
			error_out, MH_ERROR_IO, "second frame read failed");

	image.width = image.height = 2;
	image.pixels = calloc(8, 1);

	if (image.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"fixture sprite allocation");

	image.pixels[1] = 255; /* Opaque index zero. */
	image.pixels[2] = *color;
	image.pixels[3] = 255;
	image.pixels[4] = (unsigned char)frame;
	image.pixels[5] = 255;

	/* Last pixel is transparent. */
	fixture->frames++;
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_collection(void *context, void *state)
{
	struct fixture *fixture;

	fixture = context;
	fixture->closes++;
	free(state);
}

static int
check_pages(const struct mh_sprite_collection *collection, int patched)
{
	static const unsigned int pages[] = {MH_MENU_LEAGUE_SETUP,
		MH_MENU_LEAGUE_STANDINGS, MH_MENU_LEAGUE_NEW,
		MH_MENU_LEAGUE_LOAD, MH_MENU_LEAGUE_DELETE};

	struct mh_menu_options_art options;
	struct mh_menu_sound_art sound;
	struct mh_menu_graphics_art graphics;
	struct mh_menu_personal_art personal;
	struct mh_menu_credits_art credits;
	struct mh_menu_rankings_art rankings;
	struct mh_menu_controls_art controls;
	struct mh_menu_multi_art multi;
	struct mh_menu_single_art single;
	struct mh_menu_race_art race;
	struct mh_menu_car_art car;
	struct mh_menu_ghost_art ghost;
	struct mh_menu_league_art league;
	unsigned int i;

	CHECK(mh_menu_options_art_read(collection, &options, NULL) ==
		MH_ERROR_NONE);
	CHECK(options.count[1] == 2);
	mh_menu_options_art_free(&options);
	CHECK(mh_menu_sound_art_read(collection, &sound, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.numa.pixels[2] == 73 && sound.numb.pixels[1] == 255);
	mh_menu_sound_art_free(&sound);
	CHECK(mh_menu_graphics_art_read(collection, &graphics, NULL) ==
		MH_ERROR_NONE);
	mh_menu_graphics_art_free(&graphics);
	CHECK(mh_menu_personal_art_read(collection, &personal, NULL) ==
		MH_ERROR_NONE);
	CHECK(personal.patched == patched);
	mh_menu_personal_art_free(&personal);
	CHECK(mh_menu_credits_art_read(collection, &credits, NULL) ==
		MH_ERROR_NONE);
	mh_menu_credits_art_free(&credits);
	CHECK(mh_menu_rankings_art_read(collection, &rankings, NULL) ==
		MH_ERROR_NONE);
	CHECK(rankings.marker_count == 1 && rankings.marker[0].pixels[2] == 73);
	mh_menu_rankings_art_free(&rankings);
	CHECK(mh_menu_controls_art_read(collection, &controls, NULL) ==
		MH_ERROR_NONE);
	CHECK(controls.preferences == (unsigned int)patched);
	mh_menu_controls_art_free(&controls);
	CHECK(mh_menu_multi_art_read(collection, &multi, NULL) ==
		MH_ERROR_NONE);
	mh_menu_multi_art_free(&multi);
	CHECK(mh_menu_single_art_read(collection, &single, NULL) ==
		MH_ERROR_NONE);
	mh_menu_single_art_free(&single);
	CHECK(mh_menu_race_art_read(collection, &race, NULL) == MH_ERROR_NONE);
	CHECK(race.palette[0] == 73 && race.palette[3] == 73 &&
		race.palette[767] == 119);
	mh_menu_race_art_free(&race);
	CHECK(mh_menu_car_art_read(collection, &car, NULL) == MH_ERROR_NONE);
	CHECK(car.palette[0] == 73 && car.palette[767] == 119);
	mh_menu_car_art_free(&car);
	CHECK(mh_menu_ghost_art_read(collection, &ghost, NULL) ==
		MH_ERROR_NONE);
	CHECK(ghost.patched == patched && ghost.marker_count == 1);
	mh_menu_ghost_art_free(&ghost);

	for (i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
		CHECK(mh_menu_league_art_read(collection, pages[i], &league,
			NULL) == MH_ERROR_NONE);
		CHECK(league.page == pages[i]);
		mh_menu_league_art_free(&league);
	}

	return 0;
}

static int
check_art_limits(
	const struct mh_sprite_collection *collection, struct fixture *fixture)
{
	struct mh_menu_art_reader reader = {33554416UL, "bad sprite", "full"};
	struct mh_sprite_indexed images[2];
	struct mh_menu_main_art main_art, before;
	struct mh_menu_graphics_art graphics;
	struct mh_error error;
	unsigned int count, frames;

	memset(images, 0, sizeof(images));
	CHECK(mh_menu_art_read(&reader, collection, "dots", images, 2,
		MH_MENU_ART_ALL_FRAMES, &count, &error) == MH_ERROR_NONE);
	CHECK(count == 2 && reader.used == 33554432UL);
	free(images[0].pixels);
	free(images[1].pixels);
	memset(images, 0, sizeof(images));
	frames = fixture->frames;
	CHECK(mh_menu_art_read(&reader, collection, "msel", images, 2,
		MH_MENU_ART_ALL_FRAMES, &count, &error) == MH_ERROR_NONE);
	CHECK(count == 0 && error.code == MH_ERROR_NONE);
	CHECK(mh_menu_art_read(&reader, collection, "dots", images, 2,
		MH_MENU_ART_ALL_FRAMES, &count,
		&error) == MH_ERROR_RANGE);
	CHECK(strcmp(error.message, "full") == 0 && fixture->frames == frames);
	CHECK(images[0].pixels == NULL && count == 0);
	reader.used = 33554424UL;
	CHECK(mh_menu_art_read(&reader, collection, "dots", images, 1,
		MH_MENU_ART_FIRST_FRAME, &count, NULL) == MH_ERROR_NONE);
	CHECK(count == 1 && reader.used == 33554432UL);
	free(images[0].pixels);

	memset(&main_art, 0xa5, sizeof(main_art));
	before = main_art;
	fixture->dots_frames = MH_MENU_MAIN_FRAMES + 1;
	CHECK(mh_menu_main_art_read(collection, &main_art, &error) ==
		MH_ERROR_INVALID);
	CHECK(strcmp(error.message, "invalid main menu sprite") == 0);
	CHECK(memcmp(&main_art, &before, sizeof(main_art)) == 0);
	CHECK(mh_menu_graphics_art_read(collection, &graphics, NULL) ==
		MH_ERROR_NONE);
	CHECK(graphics.images[1].pixels[4] == 0);
	mh_menu_graphics_art_free(&graphics);
	fixture->dots_frames = 0;
	CHECK(mh_menu_graphics_art_read(collection, &graphics, NULL) ==
		MH_ERROR_INVALID);
	fixture->dots_frames = 2;
	fixture->dots_width = 4097;
	CHECK(mh_menu_main_art_read(collection, &main_art, NULL) ==
		MH_ERROR_INVALID);
	fixture->dots_width = 2;
	fixture->dots_height = 1;
	CHECK(mh_menu_main_art_read(collection, &main_art, NULL) ==
		MH_ERROR_INVALID);
	fixture->dots_height = 2;

	fixture->fail_second_frame = 1;
	frames = fixture->frames;
	CHECK(mh_menu_main_art_read(collection, &main_art, &error) ==
		MH_ERROR_IO);
	CHECK(strcmp(error.message, "second frame read failed") == 0);
	CHECK(fixture->frames == frames + 1);
	CHECK(memcmp(&main_art, &before, sizeof(main_art)) == 0);
	CHECK(mh_menu_graphics_art_read(collection, &graphics, NULL) ==
		MH_ERROR_NONE);
	mh_menu_graphics_art_free(&graphics);
	fixture->fail_second_frame = 0;

	return 0;
}

int
main(void)
{
	static const struct mh_sprite_source_ops ops = {open_collection,
		sprite_info, sprite_palette, sprite_indexed, NULL,
		close_collection};

	static const unsigned char bytes = 73;
	struct mh_sprite_source_provider provider;
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct mh_sprite_collection *collection;
	struct mh_menu_main_art main_art, before;
	struct mh_menu_car_art car, car_before;
	struct mh_sprite_indexed dialog;
	struct fixture fixture;
	struct mh_error error;
	unsigned int frames;

	memset(&fixture, 0, sizeof(fixture));
	fixture.palette_count = MH_IMAGE_PALETTE_COLORS;
	fixture.dots_frames = fixture.dots_width = fixture.dots_height = 2;
	provider.ops = &ops;
	provider.context = &fixture;
	entry.path = "converted/menu.frames";
	entry.data = &bytes;
	entry.size = 1;
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_sprite_collection_open(&provider, resources, "converted",
		"Data/MENU.SPR", MH_PICTURE_INDEXED, &collection,
		NULL) == MH_ERROR_NONE);
	CHECK(check_art_limits(collection, &fixture) == 0);
	CHECK(check_pages(collection, 0) == 0);
	fixture.patched = 1;
	CHECK(check_pages(collection, 1) == 0);
	CHECK(mh_menu_main_art_read(collection, &main_art, NULL) ==
		MH_ERROR_NONE);
	CHECK(main_art.count[1] == 2 && main_art.count[3] == 0);
	CHECK(main_art.images[1][1].pixels[4] == 1);
	before = main_art;
	frames = fixture.frames;
	fixture.fail_info = 1;
	CHECK(mh_menu_main_art_read(collection, &main_art, &error) ==
		MH_ERROR_IO);
	CHECK(error.code == MH_ERROR_IO && fixture.frames > frames);
	CHECK(memcmp(&main_art, &before, sizeof(main_art)) == 0);
	fixture.fail_info = 0;
	fixture.fail_frame = 1;
	CHECK(mh_menu_main_art_read(collection, &main_art, &error) ==
		MH_ERROR_IO);
	CHECK(memcmp(&main_art, &before, sizeof(main_art)) == 0);
	fixture.fail_frame = 0;
	fixture.palette_count = 2;
	memset(&car, 0xa5, sizeof(car));
	car_before = car;
	CHECK(mh_menu_car_art_read(collection, &car, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&car, &car_before, sizeof(car)) == 0);
	CHECK(mh_sprite_collection_indexed(
		collection, "req", 0, &dialog, NULL) == MH_ERROR_NONE);
	mh_sprite_collection_close(collection);
	mh_memory_resources_close(resources);
	CHECK(fixture.opens == 1 && fixture.closes == 1);
	CHECK(main_art.images[1][0].pixels[0] == 0 &&
		main_art.images[1][0].pixels[1] == 255);
	CHECK(dialog.pixels[2] == 73 && dialog.pixels[7] == 0);
	free(dialog.pixels);
	mh_menu_main_art_free(&main_art);
	puts("Menu sprite source: all pages, patched bindings, palettes, coverage, owned frames and failure cleanup passed");

	return 0;
}
