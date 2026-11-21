#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/retro/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "engine/assets/retro.h"
#include "engine/core/error.h"
#include "engine/ui/retro.h"
#include "interfaces/retro/source.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_retro_draw draw;
static struct mh_retro_assets assets;
static struct mh_hud_assets hud;

static void
prepare_font(void)
{
	unsigned int i;

	hud.atlas.image.width = hud.atlas.image.height = 512;
	hud.hardware_height = 12;
	hud.hardware_cell_width = 16;

	for (i = 0; i < 256; i++) {
		hud.hardware_glyphs[i].x = i % 16 * 16;
		hud.hardware_glyphs[i].y = i / 16 * 12;
		hud.hardware_glyphs[i].width = 8;
		hud.hardware_glyphs[i].height = 12;
		hud.hardware_advance[i] = 9;
	}
}

int
main(void)
{
	struct mh_retro_state state;
	struct mh_retro_vertex backdrop[30], saved[30];
	unsigned int i;

	prepare_font();

	CHECK(mh_retro_backdrop(960, 640, .85f, backdrop, NULL) ==
		MH_ERROR_NONE);
	CHECK(backdrop[0].alpha == 0 && backdrop[2].alpha == 255 &&
		backdrop[0].depth == 63000);
	CHECK(backdrop[6].xy[0] == 0 && backdrop[7].xy[0] == 960 &&
		backdrop[6].alpha == 255);
	CHECK(backdrop[18].rgb[2] == 255 && backdrop[18].alpha == 32 &&
		backdrop[20].alpha == 255);
	CHECK(backdrop[24].xy[0] == 0 && backdrop[24].alpha == 255 &&
		backdrop[26].alpha == 32);
	memcpy(saved, backdrop, sizeof(saved));
	CHECK(mh_retro_backdrop(0, 640, .85f, backdrop, NULL) !=
			MH_ERROR_NONE &&
		memcmp(saved, backdrop, sizeof(saved)) == 0);
	memset(&state, 0, sizeof(state));

	for (i = 0; i < MH_RETRO_LINES; i++)
		strcpy((char *)assets.text[i], "AB");

	CHECK(mh_retro_draw(&assets, &hud, 960, 640, 0, &state, &draw, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw.count == 78 && draw.text_end == 78 && draw.next.time == 0);
	CHECK(draw.vertices[30].vertex.xy[0] == 453);
	CHECK(draw.vertices[30].vertex.alpha == 255);

	/* Maximum story length, including zero-width glyphs, stays bounded. */
	for (i = 0; i < MH_RETRO_LINES; i++) {
		memset(assets.text[i], 'A', MH_RETRO_LINE_BYTES - 1);
		assets.text[i][MH_RETRO_LINE_BYTES - 1] = 0;
	}

	hud.hardware_glyphs['A'].width = 0;
	hud.hardware_advance['A'] = 1;
	CHECK(mh_retro_draw(&assets, &hud, 16384, 16384, 60, &state, &draw,
		NULL) == MH_ERROR_NONE);
	CHECK(draw.count == 30 + 4 * 49 * 6 &&
		draw.count <= MH_RETRO_MAX_VERTICES);
	memset(assets.text[3], 'A', MH_RETRO_LINE_BYTES);
	CHECK(mh_retro_draw(&assets, &hud, 960, 640, 0, &state, &draw, NULL) !=
			MH_ERROR_NONE &&
		draw.count == 0);
	assets.text[3][49] = 0;
	hud.hardware_glyphs['A'].x = 512;
	CHECK(mh_retro_draw(&assets, &hud, 960, 640, 0, &state, &draw, NULL) !=
			MH_ERROR_NONE &&
		draw.count == 0);
	puts("Retro backdrop order, centered font, bounded assembly and failure publication pass");

	return 0;
}
