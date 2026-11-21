#include "engine/ui/hud.h"
#include "engine/ui/pause.h"

static int
draw_centered_text(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	const char *text, int x, int y, unsigned long rgb)
{
	const unsigned char *p;
	float color[4];
	unsigned int width;

	width = 0;

	for (p = (const unsigned char *)text; *p != 0; p++)
		width += assets->hardware_advance[*p];

	x -= (int)(width / 2);
	color[0] = ((rgb >> 16) & 255) / 255.0f;
	color[1] = ((rgb >> 8) & 255) / 255.0f;
	color[2] = (rgb & 255) / 255.0f;
	color[3] = 1;

	for (p = (const unsigned char *)text; *p != 0; p++) {
		if (!mh_ui_quad(list, &assets->atlas,
			&assets->hardware_glyphs[*p], (float)x, (float)y,
			color))
			return 0;

		x += (int)assets->hardware_advance[*p];
	}

	return 1;
}

int
mh_pause_draw(const struct mh_hud_assets *assets,
	const struct mh_pause_state *state,
	const struct mh_pause_settings *display, unsigned int mode,
	int network_host, struct mh_ui_list *list)
{
	struct mh_pause_layout layout;
	const struct mh_pause_line *line;
	unsigned int i, saved_count;
	int x, y;

	if (assets == NULL || list == NULL || list->width == 0 ||
		list->height == 0 || list->width > MH_HUD_VIEW_LIMIT ||
		list->height > MH_HUD_VIEW_LIMIT ||
		list->count > MH_UI_MAX_QUADS * 6 ||
		assets->hardware_height == 0 ||
		assets->hardware_height > MH_HUD_FONT_LIMIT)
		return 0;

	if (mh_pause_layout(state, display, mode, network_host, &layout,
		NULL) != MH_ERROR_NONE)
		return 0;

	if (layout.count == 0)
		return 1;

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (assets->hardware_advance[i] > MH_HUD_FONT_LIMIT)
			return 0;

	saved_count = list->count;
	x = (int)(list->width / 2);
	y = (int)(list->height / 2) -
		(int)((layout.count / 2) * assets->hardware_height);

	for (i = 0; i < layout.count; i++) {
		line = &layout.lines[i];

		if (!draw_centered_text(
			assets, list, line->text, x + 1, y + 1, 0) ||
			!draw_centered_text(
				assets, list, line->text, x, y, line->color)) {
			list->count = saved_count;
			return 0;
		}

		y += (int)assets->hardware_height;
	}

	return 1;
}
