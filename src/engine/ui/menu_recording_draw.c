#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_recording_draw.h"

/* The editor-wide bound permits 255 glyph advances at the u16 maximum. */
#define RECORDING_MAX_CURSOR_WIDTH 16711425UL
#define RECORDING_MAX_CANVAS_BYTES 16777216UL

enum {
	LABEL_X = 64,
	TITLE_Y = 200,
	NAME_Y = 220,
	TITLE_COLOR = 1,
	NAME_COLOR = 2
};

enum mh_error_code
mh_menu_recording_render(const struct mh_menu_recording_output *output,
	const struct mh_font *font, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	static const unsigned char title[] = "Enter name for recorded race:";
	static const unsigned char cursor[] = "|";
	struct mh_menu_canvas work;
	enum mh_error_code status;

	if (output == NULL || output->draw_name > 1 || output->cursor > 1 ||
		output->cursor_width > RECORDING_MAX_CURSOR_WIDTH ||
		memchr(output->display_name, 0, sizeof(output->display_name)) ==
			NULL ||
		canvas == NULL || canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > RECORDING_MAX_CANVAS_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording name drawing");

	if (!output->draw_name)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);

	if (work.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory drawing recording name");

	memcpy(work.pixels, canvas->pixels, work.size);
	status = mh_menu_text_draw(font, title, sizeof(title), LABEL_X, TITLE_Y,
		TITLE_COLOR, &work, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(font, output->display_name,
			sizeof(output->display_name), LABEL_X, NAME_Y,
			NAME_COLOR, &work, error_out);

	if (status == MH_ERROR_NONE && output->cursor)
		status = mh_menu_text_draw(font, cursor, sizeof(cursor),
			LABEL_X + (long)output->cursor_width, NAME_Y,
			NAME_COLOR, &work, error_out);

	if (status == MH_ERROR_NONE)
		memcpy(canvas->pixels, work.pixels, work.size);

	free(work.pixels);

	return status;
}
