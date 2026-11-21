#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/font.h"
#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "engine/render/viewport.h"
#include "engine/ui/loading_art.h"
#include "engine/ui/loading_assets.h"
#include "engine/ui/loading_resize.h"
#include "engine/ui/loading_screen.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_resize(void)
{
	unsigned char pixels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255,
		255, 255, 255, 255, 0};

	struct mh_image source, output, before;

	source.width = source.height = 2;
	source.rgba = pixels;
	memset(&output, 0, sizeof(output));
	CHECK(mh_loading_image_resize(&source, 3, 3, 1, &output, NULL) ==
		MH_ERROR_NONE);

	if (output.rgba != NULL) {
		/*
		 * Two RGB565 truncations, not full-precision RGB interpolation.
		 */
		CHECK(output.rgba[16] == 123 && output.rgba[17] == 125 &&
			output.rgba[18] == 123 && output.rgba[19] == 0);
		CHECK(memcmp(output.rgba, pixels, 4) == 0);
		CHECK(memcmp(output.rgba + 32, pixels + 12, 4) == 0);
		free(output.rgba);
	}

	output = source;
	before = output;
	CHECK(mh_loading_image_resize(&source, 1, 3, 0, &output, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void
prepare_track(struct mh_track *track)
{
	memset(track, 0, sizeof(*track));
	strcpy(track->name, "RedRock");
	strcpy(track->location, "Arizona, USA");
	strcpy(track->length, "4.7 km");
	strcpy(track->length_us, "2.9 miles");
}

static void
check_art(void)
{
	unsigned char indices[MH_LOADING_INDICATOR_WIDTH *
		MH_LOADING_INDICATOR_HEIGHT * MH_LOADING_FRAMES];
	struct mh_indexed_image source;
	struct mh_image image, before;
	struct mh_loading_indicator indicator;
	unsigned int frame, i;

	memset(&source, 0, sizeof(source));
	memset(indices, 0, sizeof(indices));
	source.width = MH_LOADING_INDICATOR_WIDTH;
	source.height = MH_LOADING_INDICATOR_HEIGHT * MH_LOADING_FRAMES;
	source.indices = indices;
	source.size = sizeof(indices);
	source.colors = 3;

	/* No file container: duplicate black colors have distinct coverage. */
	indices[0] = 1;
	indices[12 * 11 + 1] = 2;
	source.palette[6] = 83;
	CHECK(mh_loading_indicators_prepare(&source, &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(image.width == 12 && image.height == 33);

	for (frame = 0; frame < MH_LOADING_FRAMES; frame++) {
		i = frame * 12 * 11 * 4;
		CHECK(image.rgba[i] == 0 && image.rgba[i + 3] == 255);
		CHECK(image.rgba[i + 7] == 0);
	}

	CHECK(image.rgba[(12 * 11 + 1) * 4] == 83);
	before = image;
	source.size--;
	CHECK(mh_loading_indicators_prepare(&source, &image, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&before, &image, sizeof(image)) == 0);
	source.size++;
	indices[1] = 3;
	CHECK(mh_loading_indicators_prepare(&source, &image, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&before, &image, sizeof(image)) == 0);
	free(image.rgba);
	CHECK(mh_loading_indicator_layout(640, 400, 5, 1, &indicator, NULL) ==
		MH_ERROR_NONE);
	CHECK(indicator.x == 611 && indicator.y == 370);
	CHECK(indicator.width == 12 && indicator.height == 11);
	CHECK(mh_loading_indicator_layout(640, 400, 6, 1, &indicator, NULL) ==
		MH_ERROR_INVALID);
}

static void
check_drawing(void)
{
	static struct mh_ui_list list;
	struct mh_loading_assets assets;
	struct mh_loading_layout layout;
	struct mh_loading_progress progress;
	struct mh_track track;
	unsigned char pixel[4] = {255, 255, 255, 255};
	unsigned int i, count;

	prepare_track(&track);
	CHECK(mh_loading_layout(&track, "Red Rock", 0, 61239, 185678, 640, 400,
		MH_RENDER_SOFTWARE, &layout, NULL) == MH_ERROR_NONE);
	memset(&assets, 0, sizeof(assets));
	assets.profile = MH_RENDER_SOFTWARE;
	assets.atlas.image.width = assets.atlas.image.height = 1;
	assets.atlas.image.rgba = pixel;
	assets.background.width = assets.background.height = 1;
	assets.word_font.bytes = pixel;
	mh_ui_begin(&list, 640, 400);
	CHECK(mh_loading_draw(&assets, &layout, &list, NULL) == MH_ERROR_NONE);
	CHECK(list.count == 6 && list.text_count == 10);
	CHECK(list.texts[0].x == 310 && list.texts[0].y == 10);
	CHECK(list.texts[0].foreground == MH_U16_MAX);
	CHECK(list.texts[6].y == 60 && list.texts[7].y == 70);
	CHECK(list.texts[8].y == 60 && list.texts[9].y == 70);

	for (i = 0; i < list.text_count; i++)
		CHECK(list.texts[i].before == 6);

	assets.profile = MH_RENDER_GLIDE;

	for (i = 0; i < MH_FONT_GLYPHS; i++) {
		assets.glyphs[i] = assets.background;
		assets.advance[i] = 2;
	}

	mh_ui_begin(&list, 640, 400);
	CHECK(mh_loading_draw(&assets, &layout, &list, NULL) == MH_ERROR_NONE);
	CHECK(list.count > 6 && list.text_count == 0);
	count = list.count;
	layout.rows[4].label = NULL;
	CHECK(mh_loading_draw(&assets, &layout, &list, NULL) ==
		MH_ERROR_INVALID);
	CHECK(list.count == count && list.text_count == 0 &&
		list.text_size == 0);

	mh_loading_progress_init(&progress);
	CHECK(progress.remaining == MH_LOADING_STAGES && progress.marked == 0);
	CHECK(mh_loading_progress_step(&progress, 123, NULL) == MH_ERROR_NONE);
	CHECK(progress.remaining == 5 && progress.marked == 32);
	mh_ui_begin(&list, 640, 400);
	CHECK(mh_loading_draw_progress(&assets, &progress, &list, NULL) ==
		MH_ERROR_NONE);
	CHECK(list.count == 0);
	assets.profile = MH_RENDER_SOFTWARE;
	assets.indicator = assets.background;
	CHECK(mh_loading_draw_progress(&assets, &progress, &list, NULL) ==
		MH_ERROR_NONE);
	CHECK(list.count == 12);
	CHECK(list.vertices[0].position[0] == 593 &&
		list.vertices[0].position[1] == 370);
	CHECK(list.vertices[6].position[0] == 611);
	list.count = MH_UI_MAX_QUADS * 6 - 6;
	count = list.count;
	CHECK(mh_loading_draw_progress(&assets, &progress, &list, NULL) ==
		MH_ERROR_RANGE);
	CHECK(list.count == count);
}

static void
check_layout(void)
{
	struct mh_loading_layout layout, before;
	struct mh_track track;
	unsigned int i;
	static const int rows[MH_LOADING_ROWS] = {10, 30, 40, 60, 70};

	prepare_track(&track);
	CHECK(mh_loading_layout(&track, "Red Rock", 0, 61239, 185678, 640, 400,
		MH_RENDER_SOFTWARE, &layout, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(layout.rows[0].value, "Red Rock") == 0);
	CHECK(strcmp(layout.rows[1].value, "4.7 km") == 0);
	CHECK(strcmp(layout.rows[2].value, "Arizona, USA") == 0);
	CHECK(strcmp(layout.rows[3].value, "1:01:23") == 0);
	CHECK(strcmp(layout.rows[4].value, "3:05:67") == 0);

	for (i = 0; i < MH_LOADING_ROWS; i++) {
		CHECK(layout.rows[i].label_x == 310);
		CHECK(layout.rows[i].value_x == 460);
		CHECK(layout.rows[i].y == rows[i]);
	}

	CHECK(mh_loading_layout(&track, "Red Rock", 1, 0, MH_U32_MAX, 1280, 800,
		MH_RENDER_GLIDE, &layout, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(layout.rows[1].value, "2.9 miles") == 0);
	CHECK(strcmp(layout.rows[3].value, "0:00:00") == 0);
	CHECK(strcmp(layout.rows[4].value, "71582:47:29") == 0);

	for (i = 0; i < MH_LOADING_ROWS; i++) {
		CHECK(layout.rows[i].label_x == 680);
		CHECK(layout.rows[i].value_x == 960);
		CHECK(layout.rows[i].y == rows[i] * 2);
	}

	before = layout;
	CHECK(mh_loading_layout(&track, "Red Rock", 2, 0, 0, 640, 400,
		MH_RENDER_SOFTWARE, &layout, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&before, &layout, sizeof(layout)) == 0);
	memset(track.length, 'a', sizeof(track.length));
	CHECK(mh_loading_layout(&track, "Red Rock", 0, 0, 0, 640, 400,
		MH_RENDER_SOFTWARE, &layout, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&before, &layout, sizeof(layout)) == 0);
}

int
main(void)
{

	check_resize();
	check_art();
	check_drawing();
	check_layout();

	return failures != 0;
}
