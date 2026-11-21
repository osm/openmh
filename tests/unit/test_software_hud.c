#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/binary.h"
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

static void
prepare_assets(unsigned int width, unsigned int height)
{
	unsigned int i;

	memset(&assets, 0, sizeof(assets));
	assets.resolution_shift = width < 640;
	assets.font.height = 10;
	assets.atlas.image.width = 8192;
	assets.atlas.image.height = 2048;

	for (i = 0; i < MH_SOFTWARE_HUD_SPRITES; i++) {
		assets.sprites[i].x = i * 128;
		assets.sprites[i].width = 20;
		assets.sprites[i].height = 20;
	}

	mh_ui_begin(&list, width, height);
}

int
main(void)
{
	struct mh_hud_state state;
	struct mh_error error;
	struct mh_ui_vertex saved_vertex;
	unsigned int low_resolution, available_quads;

	memset(&state, 0, sizeof(state));
	state.info_detail = 2;
	state.checkpoint_info = 2;
	state.speed = 123;
	state.laps = 5;
	state.lap = 2;
	state.cars = 8;
	state.position = 4;
	state.revs = .5;

	for (low_resolution = 0; low_resolution < 2; low_resolution++) {
		prepare_assets(
			low_resolution ? 320 : 640, low_resolution ? 200 : 480);
		CHECK(mh_software_hud_panel(&assets, &state, &list, &error) ==
			MH_ERROR_NONE);
		CHECK(error.message == NULL && list.count > 6);
		CHECK(list.vertices[0].position[0] ==
			(low_resolution ? 34 : 68));
		CHECK(list.vertices[0].position[1] ==
			(low_resolution ? 5 : 10));
		saved_vertex = list.vertices[0];
		list.text_count = 1;
		list.text_size = 4;

		for (available_quads = 0; available_quads < 6;
			available_quads++) {
			list.count = MH_UI_MAX_QUADS * 6 - available_quads * 6;
			CHECK(mh_software_hud_panel(&assets, &state, &list,
				NULL) == MH_ERROR_RANGE);
			CHECK(list.count ==
				MH_UI_MAX_QUADS * 6 - available_quads * 6);
			CHECK(list.text_count == 1 && list.text_size == 4);
			CHECK(memcmp(&saved_vertex, &list.vertices[0],
				sizeof(saved_vertex)) == 0);
		}

		list.count = 6;
		state.info_detail = 3;
		CHECK(mh_software_hud_panel(&assets, &state, &list, NULL) ==
			MH_ERROR_INVALID);
		CHECK(list.count == 6);
		state.info_detail = 0;
		CHECK(mh_software_hud_panel(&assets, &state, &list, NULL) ==
			MH_ERROR_NONE);
		CHECK(list.count == 6);
		state.info_detail = 2;
		assets.resolution_shift ^= 1;
		CHECK(mh_software_hud_panel(&assets, &state, &list, NULL) ==
			MH_ERROR_INVALID);
		assets.resolution_shift ^= 1;
		assets.sprites[MH_SOFTWARE_HUD_METER_EMPTY].width--;
		CHECK(mh_software_hud_panel(&assets, &state, &list, NULL) ==
			MH_ERROR_INVALID);
		CHECK(list.count == 6);
	}

	puts("software HUD panel guards and append rollback pass");

	return 0;
}
