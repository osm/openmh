#include "engine/core/limits.h"
#include "engine/ui/loading_art.h"
#include "engine/ui/loading_assets.h"

#define HARDWARE_ADVANCE_LIMIT 17

static const float white[4] = {1, 1, 1, 1};

enum mh_error_code
mh_loading_draw_progress(const struct mh_loading_assets *assets,
	const struct mh_loading_progress *progress, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	struct mh_loading_indicator indicator;
	unsigned int stage, column, saved_vertices;
	enum mh_error_code status;

	if (assets == NULL || progress == NULL || list == NULL ||
		progress->marked >= (1U << MH_LOADING_STAGES))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading indicators");

	if (assets->profile == MH_RENDER_GLIDE ||
		assets->profile == MH_RENDER_DIRECT3D)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (assets->profile != MH_RENDER_SOFTWARE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading renderer");

	saved_vertices = list->count;
	status = MH_ERROR_NONE;

	for (stage = 0; stage < MH_LOADING_STAGES; stage++) {
		if (!(progress->marked & (1U << stage)))
			continue;

		for (column = 0; column < MH_LOADING_COLUMNS; column++) {
			status = mh_loading_indicator_layout(list->width,
				list->height, stage, column, &indicator,
				error_out);

			if (status == MH_ERROR_NONE &&
				!mh_ui_quad_scaled(list, &assets->atlas,
					&assets->indicator, (float)indicator.x,
					(float)indicator.y,
					(float)indicator.width,
					(float)indicator.height, white))
				status = mh_error_set(error_out, MH_ERROR_RANGE,
					"loading indicators exceed UI capacity");

			if (status != MH_ERROR_NONE) {
				list->count = saved_vertices;
				return status;
			}
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
draw_text(const struct mh_loading_assets *assets, const char *text, int x,
	int y, struct mh_ui_list *list, struct mh_error *error_out)
{
	const struct mh_ui_region *glyph;
	unsigned int glyph_index, code;
	size_t length;

	if (text == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing loading caption");

	for (length = 0; length < MH_TRACK_TEXT_SIZE && text[length] != 0;
		length++)
		;

	if (length == MH_TRACK_TEXT_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated loading caption");

	if (assets->profile == MH_RENDER_SOFTWARE)
		return mh_ui_word_text(list, &assets->word_font,
			(const unsigned char *)text, length, x, y, MH_U16_MAX,
			MH_UI_TEXT_LEFT, error_out);

	for (glyph_index = 0; glyph_index < length; glyph_index++) {
		code = (unsigned char)text[glyph_index];
		glyph = &assets->glyphs[code];

		if (assets->advance[code] > HARDWARE_ADVANCE_LIMIT ||
			!mh_ui_quad(list, &assets->atlas, glyph, (float)x,
				(float)y, white))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"loading caption exceeds UI capacity");

		x += (int)assets->advance[code];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_loading_draw(const struct mh_loading_assets *assets,
	const struct mh_loading_layout *layout, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	/* Pair the first three captions, then draw both record labels first. */
	static const unsigned int row_order[] = {0, 0, 1, 1, 2, 2, 3, 4, 3, 4};
	static const unsigned int draw_value[] = {0, 1, 0, 1, 0, 1, 0, 0, 1, 1};
	const struct mh_loading_row *row;
	unsigned int saved_vertices, saved_texts, saved_bytes, caption_index;
	enum mh_error_code status;

	if (assets == NULL || layout == NULL || list == NULL ||
		assets->atlas.image.rgba == NULL ||
		(unsigned int)assets->profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading screen draw");

	saved_vertices = list->count;
	saved_texts = list->text_count;
	saved_bytes = list->text_size;

	if (!mh_ui_quad_scaled(list, &assets->atlas, &assets->background, 0, 0,
		(float)list->width, (float)list->height, white))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"loading background exceeds UI capacity");

	status = MH_ERROR_NONE;

	for (caption_index = 0;
		caption_index < sizeof(row_order) / sizeof(row_order[0]) &&
		status == MH_ERROR_NONE;
		caption_index++) {
		row = &layout->rows[row_order[caption_index]];
		status = draw_text(assets,
			draw_value[caption_index] ? row->value : row->label,
			draw_value[caption_index] ? row->value_x : row->label_x,
			row->y, list, error_out);
	}

	if (status != MH_ERROR_NONE) {
		list->count = saved_vertices;
		list->text_count = saved_texts;
		list->text_size = saved_bytes;
		return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_loading_draw_ready(const struct mh_loading_assets *assets,
	struct mh_ui_list *list, struct mh_error *error_out)
{
	if (assets == NULL || list == NULL || list->width < 320 ||
		list->height < 200)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid loading prompt");

	return draw_text(assets, "Press any key to continue", 12,
		(int)list->height - 30, list, error_out);
}
