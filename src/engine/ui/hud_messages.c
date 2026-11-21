#include <stddef.h>

#include "engine/core/error.h"
#include "engine/ui/hud.h"

int
mh_hud_messages_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list)
{
	struct mh_message_view view;
	const struct mh_message *message;
	const unsigned char *p;
	unsigned int i;
	int x;
	float color[4];

	if (assets == NULL || state == NULL || list == NULL)
		return 0;

	if (state->messages == NULL)
		return 1;

	if (mh_messages_view(state->messages, state->message_ms, list->height,
		assets->hardware_height, &view, NULL) != MH_ERROR_NONE)
		return 0;

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (assets->hardware_advance[i] > MH_HUD_FONT_LIMIT)
			return 0;

	for (i = 0; i < view.count; i++) {
		message = &state->messages->rows[view.lines[i].row];
		color[0] = ((message->color >> 16) & 255) / 255.0f;
		color[1] = ((message->color >> 8) & 255) / 255.0f;
		color[2] = (message->color & 255) / 255.0f;
		color[3] = 1;
		x = view.lines[i].x;

		for (p = (const unsigned char *)message->text; *p != 0; p++) {
			if (!mh_ui_quad(list, &assets->atlas,
				&assets->hardware_glyphs[*p], (float)x,
				(float)view.lines[i].y, color))
				return 0;

			x += (int)assets->hardware_advance[*p];
		}
	}

	return 1;
}
