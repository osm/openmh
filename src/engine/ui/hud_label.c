#include "engine/ui/hud.h"

int
mh_hud_label(const struct mh_hud_assets *assets, const char *text, float y,
	struct mh_ui_list *list)
{
	static const float white[4] = {1, 1, 1, 1};
	static const float black[4] = {0, 0, 0, 1};
	const unsigned char *p;
	unsigned int width;
	float x;

	if (assets == NULL || text == NULL || list == NULL)
		return 0;

	width = 0;

	for (p = (const unsigned char *)text; *p != 0; p++)
		width += assets->hardware_advance[*p];

	x = ((float)list->width - width) * .5f;

	for (p = (const unsigned char *)text; *p != 0; p++) {
		if (!mh_ui_quad(list, &assets->atlas,
			&assets->hardware_glyphs[*p], x + 1, y + 1,
			black) ||
			!mh_ui_quad(list, &assets->atlas,
				&assets->hardware_glyphs[*p], x, y, white))
			return 0;

		x += assets->hardware_advance[*p];
	}

	return 1;
}
