#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/tron_draw.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
prepare_assets(struct mh_hud_assets *assets)
{
	unsigned int i;

	memset(assets, 0, sizeof(*assets));
	assets->atlas.image.width = assets->atlas.image.height = 512;
	assets->solid.x = 256;
	assets->solid.width = assets->solid.height = 1;
	assets->hardware_cell_width = 16;
	assets->hardware_height = 12;

	for (i = 0; i < 256; i++) {
		assets->hardware_glyphs[i].x = i % 16 * 16;
		assets->hardware_glyphs[i].y = i / 16 * 12;
		assets->hardware_glyphs[i].width = i == 32 ? 8 : i % 13 + 1;
		assets->hardware_glyphs[i].height = 12;
		assets->hardware_advance[i] =
			assets->hardware_glyphs[i].width + 1;
	}
}

int
main(void)
{
	static struct mh_ui_list list;
	struct mh_hud_assets assets;
	struct mh_tron_intro frame, saved;
	struct mh_ui_vertex first;

	float xy[2], alpha;

	CHECK(mh_tron_intro_frame(512, 384, 3, .5f, 1, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(frame.next_time == 3.5f && !frame.backdrop && frame.count == 0);
	CHECK(mh_tron_intro_frame(512, 384, 3.5f, 0, 1, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(frame.backdrop && frame.opacity == 127.5f && frame.count == 0);
	CHECK(mh_tron_intro_frame(512, 384, 4, 0, 1, &frame, NULL) ==
			MH_ERROR_NONE &&
		frame.count == 0);
	CHECK(mh_tron_intro_frame(512, 384, 8, 0, 1, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(frame.count == 5 && frame.lines[0].line == 1 &&
		frame.lines[4].line == 5);
	CHECK(frame.lines[4].x == 25 && frame.lines[4].y == 391);
	CHECK(mh_tron_intro_frame(512, 384, 52, 0, 1, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(frame.count == 0 && frame.backdrop && frame.opacity == 255);
	CHECK(mh_tron_intro_frame(512, 384, 52.5f, 0, 1, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(frame.count == 0 && frame.backdrop && frame.opacity == 127.5f);
	CHECK(mh_tron_intro_frame(512, 384, 53, 0, 1, &frame, NULL) ==
			MH_ERROR_NONE &&
		!frame.backdrop);
	CHECK(mh_tron_intro_frame(512, 384, 16, 1, 0, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(frame.next_time == 16 && !frame.backdrop && frame.count == 0);
	saved = frame;
	CHECK(mh_tron_intro_frame(0, 384, 16, 1, 1, &frame, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_tron_intro_frame(512, 16385, 16, 1, 1, &frame, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_tron_intro_frame(512, 384, -1, 1, 1, &frame, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_tron_intro_frame(512, 384, 16, -1, 1, &frame, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_tron_intro_frame(512, 384, 16, 1, 2, &frame, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_tron_intro_frame(512, 384, 16, 1, 1, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&frame, &saved, sizeof(frame)) == 0);
	CHECK(mh_tron_intro_text(MH_TRON_INTRO_LINES) == NULL);
	CHECK(mh_tron_intro_corner(512, 384, 25, 300, xy, &alpha, NULL) ==
			MH_ERROR_NONE &&
		alpha == 0);
	CHECK(mh_tron_intro_corner(512, 384, 25, 360, xy, &alpha, NULL) ==
			MH_ERROR_NONE &&
		alpha == 255);
	CHECK(xy[0] == 25 && xy[1] == 360);
	CHECK(mh_tron_intro_corner(512, 384, 25, 1e10f, xy, &alpha, NULL) !=
		MH_ERROR_NONE);
	CHECK(xy[0] == 25 && xy[1] == 360 && alpha == 255);
	prepare_assets(&assets);
	mh_ui_begin(&list, 512, 384);
	CHECK(mh_tron_intro_frame(512, 384, 20, 0, 1, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_tron_intro_draw(&assets, &frame, &list) && list.count > 12);
	first = list.vertices[0];
	list.count = MH_UI_MAX_QUADS * 6 - 6;
	CHECK(!mh_tron_intro_draw(&assets, &frame, &list) &&
		list.count == MH_UI_MAX_QUADS * 6 - 6);
	CHECK(memcmp(&first, list.vertices, sizeof(first)) == 0);
	list.count = 6;
	assets.hardware_glyphs['H'].x = 512;
	CHECK(!mh_tron_intro_draw(&assets, &frame, &list) && list.count == 6);
	CHECK(memcmp(&first, list.vertices, sizeof(first)) == 0);
	assets.solid.width = 0;
	CHECK(!mh_tron_intro_draw(&assets, &frame, &list) && list.count == 6);
	CHECK(!mh_tron_intro_draw(NULL, &frame, &list) && list.count == 6);
	puts("TRON introduction boundaries, font draw rollback and invalid-input preservation pass");

	return 0;
}
