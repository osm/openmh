#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/retro.h"
#include "engine/core/error.h"
#include "engine/render/retro.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_retro_assets assets, saved;

int
main(void)
{
	struct mh_retro_state state;
	struct mh_retro_frame frame, before;
	struct mh_retro_vertex glyph[4], previous[4];
	float xy[4][2];
	unsigned int i;

	memset(&state, 0, sizeof(state));
	CHECK(mh_retro_frame(0, &state, &frame, NULL) == MH_ERROR_NONE &&
		frame.next.scroll == 0 && frame.next.line == 0);
	CHECK(frame.center == .85f && frame.lines[3] == 3 &&
		frame.scroll[3] == -.75f);
	state.line = MH_RETRO_LINES - 4;
	state.scroll = 1;
	CHECK(mh_retro_frame(60, &state, &frame, NULL) == MH_ERROR_NONE &&
		frame.next.line == 1 && frame.next.scroll == 1);
	CHECK(frame.lines[3] == MH_RETRO_LINES - 1 && frame.next.wave == 120 &&
		frame.next.time == 60);
	before = frame;
	state.scroll = 2;
	CHECK(mh_retro_frame(0, &state, &frame, NULL) != MH_ERROR_NONE &&
		memcmp(&frame, &before, sizeof(frame)) == 0);
	memset(&state, 0, sizeof(state));

	for (i = 0; i < 100000; i++) {
		CHECK(mh_retro_frame(1.0f / 60, &state, &frame, NULL) ==
			MH_ERROR_NONE);
		CHECK(frame.next.line < MH_RETRO_LINES - 3 &&
			frame.next.scroll >= 0 && frame.next.scroll <= 1);
		state = frame.next;
	}

	memset(&assets, 0x61, sizeof(assets));
	saved = assets;
	CHECK(mh_retro_mhretro_decode(NULL, &assets, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&assets, &saved, sizeof(assets)) == 0);
	memset(xy, 0, sizeof(xy));
	CHECK(mh_retro_glyph(960, 640, 12, .85f, .5f, 0, (const float (*)[2])xy,
		glyph, NULL) == MH_ERROR_NONE);
	CHECK(glyph[0].xy[0] == 480 && glyph[0].xy[1] == 544 &&
		glyph[0].depth == 64100 && glyph[0].rgb[0] == 255);
	memcpy(previous, glyph, sizeof(glyph));
	xy[3][0] = 1e20f;
	CHECK(mh_retro_glyph(960, 640, 12, .85f, .5f, 0, (const float (*)[2])xy,
		glyph, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(previous, glyph, sizeof(glyph)) == 0);
	CHECK(mh_retro_glyph(960, 640, 0, .85f, .5f, 0, (const float (*)[2])xy,
		glyph, NULL) != MH_ERROR_NONE);
	puts("Retro scroll timing, sentinel wrap, sustained updates, glyph projection and atomic rejection pass");

	return 0;
}
