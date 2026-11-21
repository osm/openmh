#include "engine/core/limits.h"
#include "engine/ui/chat_draw.h"
#include "engine/ui/hud.h"

#define TEXT_WIDTH_LIMIT 540
#define TEXT_RIGHT_MARGIN 15
#define TEXT_INSET 5
#define CURSOR_PERIOD_MS 1000
#define CURSOR_HIDDEN_MS 500

int
mh_chat_draw(const struct mh_hud_assets *assets, const char *text,
	unsigned long milliseconds, struct mh_ui_list *list)
{
	const unsigned char *cursor;
	unsigned int suffix_start, width, limit, glyph, component, saved_count;
	int x, y;
	float color[4];

	if (assets == NULL || list == NULL)
		return 0;

	if (text == NULL)
		return 1;

	if (milliseconds > MH_U32_MAX || list->width < MH_HUD_MIN_WIDTH ||
		list->width > MH_HUD_VIEW_LIMIT ||
		list->height < MH_HUD_MIN_HEIGHT ||
		list->height > MH_HUD_VIEW_LIMIT ||
		list->count > MH_UI_MAX_QUADS * 6 ||
		assets->hardware_height == 0 ||
		assets->hardware_height > MH_HUD_FONT_LIMIT)
		return 0;

	cursor = (const unsigned char *)text;

	for (suffix_start = 0;
		suffix_start < MH_MESSAGE_BYTES && cursor[suffix_start] != 0;
		suffix_start++) {
	}

	if (suffix_start == MH_MESSAGE_BYTES)
		return 0;

	for (glyph = 0; glyph < MH_FONT_GLYPHS; glyph++)
		if (assets->hardware_advance[glyph] > MH_HUD_FONT_LIMIT)
			return 0;

	width = 0;
	limit = list->width - TEXT_RIGHT_MARGIN < TEXT_WIDTH_LIMIT
		? list->width - TEXT_RIGHT_MARGIN
		: TEXT_WIDTH_LIMIT;

	while (suffix_start != 0) {
		width += assets->hardware_advance[cursor[suffix_start - 1]];

		if (width > limit)
			break;

		suffix_start--;
	}

	saved_count = list->count;
	x = TEXT_INSET;
	y = (int)list->height - (int)assets->hardware_height - TEXT_INSET;

	for (component = 0; component < 4; component++)
		color[component] = 1;

	for (cursor += suffix_start; *cursor; cursor++) {
		if (!mh_ui_quad(list, &assets->atlas,
			&assets->hardware_glyphs[*cursor], (float)x,
			(float)y, color)) {
			list->count = saved_count;
			return 0;
		}

		x += (int)assets->hardware_advance[*cursor];
	}

	if (milliseconds % CURSOR_PERIOD_MS >= CURSOR_HIDDEN_MS &&
		!mh_ui_quad(list, &assets->atlas, &assets->hardware_glyphs['|'],
			(float)x, (float)y, color)) {
		list->count = saved_count;
		return 0;
	}

	return 1;
}
