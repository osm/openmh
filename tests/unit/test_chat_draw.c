#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/ui/chat_draw.h"
#include "engine/ui/hud.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "chat draw line %d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)
static struct mh_hud_assets assets;
static struct mh_ui_list list;

static void
prepare_font(unsigned int height, unsigned int metric)
{
	unsigned int i;

	memset(&assets, 0, sizeof(assets));
	assets.atlas.image.width = assets.atlas.image.height = 8192;
	assets.hardware_height = height;

	for (i = 0; i < 256; i++) {
		assets.hardware_glyphs[i].x = i * 32;
		assets.hardware_glyphs[i].width = i % 7 + 3;
		assets.hardware_glyphs[i].height = height;

		if (metric == 0)
			assets.hardware_advance[i] = i % 7 + 4;
		else if (metric == 1)
			assets.hardware_advance[i] = 0;
		else if (metric == 2)
			assets.hardware_advance[i] = 256;
		else
			assets.hardware_advance[i] = i % 3 * 17;
	}
}

int
main(void)
{
	char bad[256];

	prepare_font(12, 0);
	mh_ui_begin(&list, 640, 480);
	CHECK(mh_chat_draw(&assets, "", 499, &list) && list.count == 0);
	CHECK(mh_chat_draw(&assets, "", 500, &list) && list.count == 6);
	CHECK(list.vertices[0].position[0] == 5 &&
		list.vertices[0].position[1] == 463);
	list.count = MH_UI_MAX_QUADS * 6 - 6;
	CHECK(!mh_chat_draw(&assets, "ab", 0, &list) &&
		list.count == MH_UI_MAX_QUADS * 6 - 6);
	memset(bad, 'x', sizeof(bad));
	mh_ui_begin(&list, 640, 480);
	CHECK(!mh_chat_draw(&assets, bad, 0, &list) && list.count == 0);

	return 0;
}
