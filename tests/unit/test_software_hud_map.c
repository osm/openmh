#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "engine/ui/software_hud.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_software_hud_assets assets;
static struct mh_ui_list list;
static unsigned char font_byte;

static void
prepare_assets(unsigned int width, unsigned int height)
{
	unsigned int i;

	memset(&assets, 0, sizeof(assets));
	assets.resolution_shift = width < 640;
	assets.green_bits = 6;
	assets.atlas.image.width = 4096;
	assets.atlas.image.height = 2048;
	assets.font.bytes = &font_byte;
	assets.map.width = 128;
	assets.map.height = 160;
	assets.map_info[1] = 256;

	for (i = 0; i < 8; i++) {
		assets.small_markers[i].x = (i + 1) * 128;
		assets.small_markers[i].width = 12;
		assets.small_markers[i].height = 24;
		assets.large_markers[i].x = (i + 9) * 128;
		assets.large_markers[i].width = 16;
		assets.large_markers[i].height = 30;
	}

	mh_ui_begin(&list, width, height);
}

static int
secret_lifetime(void)
{
	struct mh_hud_state state;
	struct mh_software_hud_secrets secrets;
	unsigned int i;

	prepare_assets(640, 480);
	memset(&state, 0, sizeof(state));
	memset(&secrets, 0, sizeof(secrets));
	state.map_mode = 2;
	state.marker_count = 1;
	state.markers[0].active = state.markers[0].viewed = 1;
	state.markers[0].rank = 1;
	state.markers[0].world[0] = assets.map_info[0] = 476;
	state.markers[0].world[2] = assets.map_info[2] = -188;

	for (i = 0; i < 202; i++) {
		mh_ui_begin(&list, 640, 480);
		CHECK(mh_software_hud_map(&assets, &state, "RuhrStadt",
			&secrets, &list, NULL) == MH_ERROR_NONE);
		CHECK(secrets.visits == i + 1);
		CHECK(secrets.club_credits == (int)(i == 201));
		CHECK(list.text_count == (i == 201 ? 3 : 2));
	}

	CHECK(list.texts[0].before == 6);
	CHECK(list.texts[1].before == 12);
	CHECK(list.texts[0].x == 250 && list.texts[0].y == 216);
	CHECK(list.texts[0].foreground == MH_U16_MAX);
	state.map_mode = 0;
	CHECK(mh_software_hud_map(&assets, &state, "ruhrstadt", &secrets, &list,
		NULL) == MH_ERROR_NONE);
	CHECK(secrets.visits == 202 && secrets.club_credits == 1);
	state.map_mode = 2;
	state.markers[0].world[0] += 20;
	mh_ui_begin(&list, 640, 480);
	CHECK(mh_software_hud_map(&assets, &state, "ruhrstadt", &secrets, &list,
		NULL) == MH_ERROR_NONE);
	CHECK(secrets.visits == 0 && secrets.club_credits == 1);

	/* The greeting succeeds, then a later marker fails: roll back both. */
	state.markers[0].world[0] = 476;
	secrets.visits = 201;
	secrets.club_credits = 0;
	mh_ui_begin(&list, 640, 480);
	list.text_count = MH_UI_MAX_TEXTS - 2;
	CHECK(mh_software_hud_map(&assets, &state, "ruhrstadt", &secrets, &list,
		NULL) == MH_ERROR_RANGE);
	CHECK(secrets.visits == 201 && secrets.club_credits == 0);
	CHECK(list.count == 0 && list.text_count == MH_UI_MAX_TEXTS - 2 &&
		list.text_size == 0);

	return 0;
}

int
main(void)
{
	struct mh_hud_state state;
	unsigned int i;

	CHECK(secret_lifetime() == 0);
	prepare_assets(640, 480);
	memset(&state, 0, sizeof(state));
	state.map_mode = 2;
	state.marker_count = 1;
	state.markers[0].active = state.markers[0].viewed = 1;
	state.markers[0].rank = 1;
	state.markers[0].color = 0xffcc88UL;
	CHECK(mh_software_hud_map(&assets, &state, NULL, NULL, &list, NULL) ==
		MH_ERROR_NONE);
	CHECK(list.count == 12 && list.text_count == 2 && list.text_size == 2);
	CHECK(list.text[0] == 250 && list.text[1] == 251);
	CHECK(list.texts[0].before == 12 && list.texts[1].before == 12);
	CHECK(list.texts[0].foreground == 0xfe71 &&
		list.texts[1].foreground == 0x7b28);

	/* 3.1 misses rank 9..13 sprites but still emits the colored glyphs. */
	for (i = 9; i <= 13; i++) {
		prepare_assets(640, 480);
		state.markers[0].rank = i;
		CHECK(mh_software_hud_map(&assets, &state, NULL, NULL, &list,
			NULL) == MH_ERROR_NONE);
		CHECK(list.count == 6 && list.text_count == 2);
		CHECK(list.texts[0].before == 6 && list.text[0] == 250);
	}

	state.markers[0].rank = 1;

	for (i = 0; i < 3; i++) {
		prepare_assets(640, 480);
		list.count = 6;
		list.text_count = i ? MH_UI_MAX_TEXTS - 1 : 0;
		list.text_size = i == 2 ? MH_UI_TEXT_BYTES : 0;
		list.texts[MH_UI_MAX_TEXTS - 2].before = 6;

		if (i == 0)
			list.count = MH_UI_MAX_QUADS * 6 - 6;

		CHECK(mh_software_hud_map(&assets, &state, NULL, NULL, &list,
			NULL) == MH_ERROR_RANGE);
		CHECK(list.count == (i ? 6 : MH_UI_MAX_QUADS * 6 - 6));
		CHECK(list.text_count == (i ? MH_UI_MAX_TEXTS - 1 : 0));
		CHECK(list.text_size == (i == 2 ? MH_UI_TEXT_BYTES : 0));
	}

	prepare_assets(640, 480);
	state.markers[0].world[1] = assets.map_info[1];
	CHECK(mh_software_hud_map(&assets, &state, NULL, NULL, &list, NULL) ==
		MH_ERROR_NONE);
	CHECK(list.count == 6 && list.text_count == 0);
	state.map_mode = 3;
	CHECK(mh_software_hud_map(&assets, &state, NULL, NULL, &list, NULL) ==
		MH_ERROR_INVALID);
	CHECK(list.count == 6);
	state.map_mode = 0;
	CHECK(mh_software_hud_map(&assets, &state, NULL, NULL, &list, NULL) ==
		MH_ERROR_NONE);
	CHECK(list.count == 6);
	puts("software map sprite/text ordering, colors, gates and rollback pass");

	return 0;
}
