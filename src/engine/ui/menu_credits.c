#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_credits.h"
#include "engine/ui/menu_position.h"
#include "interfaces/sprite/collection.h"

#define CREDITS_LAYOUT 18
#define LAST_ART_SLOT 3
#define TEXT_ORIGIN_SLOT 24
#define TEXT_SPACING_SLOT 25
#define SPACING_DIVISOR 8
#define ROW_CAPACITY 65536
#define TEXT_BANKS 2
#define TEXT_LINES 12
#define HEADING_COLOR 3
#define TEXT_COLOR 1

struct credit_row {
	unsigned int	offset, color;
};

void
mh_menu_credits_art_free(struct mh_menu_credits_art *art)
{
	unsigned int i;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_CREDITS_ART_SLOTS; i++)
		free(art->images[i].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_credits_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_credits_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_CREDITS_ART_SLOTS] = {
		NULL, "dots", "scr9", "credi"};

	struct mh_menu_credits_art parsed;
	enum mh_error_code status;
	struct mh_menu_art_reader reader = {0, "invalid Credits sprite",
		"Credits artwork exceeds capacity"};

	unsigned int i;

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Credits artwork archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		status = mh_menu_art_read(&reader, collection, names[i],
			&parsed.images[i], 1, MH_MENU_ART_FIRST_FRAME, NULL,
			error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_credits_art_free(&parsed);

	return status;
}

void
mh_menu_credits_animation_init(struct mh_menu_credits_animation *state)
{
	if (state == NULL)
		return;

	memset(state, 0, sizeof(*state));
	state->sprites.wobble[2] = state->sprites.wobble[3] = 1;
}

int
mh_menu_credits_alternate(int club, const struct mh_menu_credits_input *input)
{
	return club == 1 ||
		(input != NULL && input->control == 1 && input->left_alt == 1 &&
			input->right_alt == 1);
}

static void
request_main(struct mh_menu_output *result, unsigned int sound)
{
	result->events[result->count].kind = MH_MENU_SOUND;
	result->events[result->count++].value = sound;
	result->events[result->count].kind = MH_MENU_PAGE;
	result->events[result->count++].value = MH_MENU_MAIN;
}

enum mh_error_code
mh_menu_credits_update(const struct mh_menu_credits_input *input,
	struct mh_menu_output *output, struct mh_error *error_out)
{
	struct mh_menu_output result;

	if (input == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Credits input");

	memset(&result, 0, sizeof(result));

	if (input->escape == 1)
		request_main(&result, MH_MENU_SOUND_CANCEL);

	if (input->enter == 1)
		request_main(&result, MH_MENU_SOUND_CONFIRM);

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
draw_text(const struct mh_font *font, unsigned int bank, long x, long y,
	long spacing, struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	/*
	 * Strings. Byte 0x99 is the original FONT1 glyph, not a Unicode
	 * encoding of the Swedish letter in Markus's name.
	 */
	static const char *const text[TEXT_BANKS][TEXT_LINES] = {
		{"Programming:", "Joakim Grundwall - Andreas Axelsson",
			"Daniel Hansen - Daniel Hansevi - Vidar Nygren",
			"Mattias Gruvman - Mikael Rudberg - Mikael Kalms",
			"Graphics:", "Per-Anders Gustafsson - Joakim Wejdermar",
			"Patrik Bergdahl - Andreas Hansevi - Jens Oras",
			"Kenny Magnusson - Markus Nystr\231m - Nicholas Nolby",
			"Music & sfx:", "Olof Gustafsson",
			"Produced by:", "Fredrik Liliegren"},
		{"Code:", "Atlan/CRG - Goofy/TSL",
			"Eq/TBL - McCoy/SMOB - Agorn/FuN",
			"Offa/TBL - Dezed/TSL - Scout/C!S/APX/TBL",
			"Artwork:", "Rodney/TBL - Serpent/CRG",
			"Terminator/TSL - JeePen/SMOB - JOS",
			"Louie/TBL - Chevron/TSL - NeKKo",
			"Noice:", "Blaizer/TSL", "Organizing:", "Animal/TSL"}};

	static const struct credit_row rows[TEXT_LINES] = {{0, HEADING_COLOR},
		{2, TEXT_COLOR}, {3, TEXT_COLOR}, {4, TEXT_COLOR},
		{6, HEADING_COLOR}, {8, TEXT_COLOR}, {9, TEXT_COLOR},
		{10, TEXT_COLOR}, {12, HEADING_COLOR}, {14, TEXT_COLOR},
		{16, HEADING_COLOR}, {18, TEXT_COLOR}};

	const unsigned char *line;
	enum mh_error_code status;
	unsigned int i, j, width;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < TEXT_LINES && status == MH_ERROR_NONE; i++) {
		line = (const unsigned char *)text[bank][i];
		width = 0;

		for (j = 0; line[j] != 0; j++)
			width += font->advance[line[j]];

		status = mh_menu_text_draw(font, line,
			strlen(text[bank][i]) + 1, x - (long)(width / 2),
			y + (long)rows[i].offset * spacing, rows[i].color,
			canvas, error_out);
	}

	return status;
}

enum mh_error_code
mh_menu_credits_render(const struct mh_menu_credits_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	int club, const struct mh_menu_credits_input *input, unsigned int mix,
	struct mh_menu_credits_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	struct mh_menu_credits_animation next;
	struct mh_menu_sprite_output commands;
	struct mh_menu_sprite_row *rows;
	struct mh_menu_canvas work;
	enum mh_error_code status;
	unsigned int i, bank;
	unsigned long seed, phase;
	long x, y, unused, spacing;

	if (art == NULL || input == NULL || state == NULL || random == NULL ||
		noise == NULL || canvas == NULL || canvas->pixels == NULL ||
		canvas->size == 0 || canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		*random > MH_U32_MAX || *noise > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Credits frame");

	status = mh_menu_position(layout, CREDITS_LAYOUT, TEXT_ORIGIN_SLOT, mix,
		&x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, CREDITS_LAYOUT,
			TEXT_SPACING_SLOT, mix, &unused, &spacing, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(font, (const unsigned char *)"", 1,
			x, y, 1, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* C89 leaves negative division rounding implementation-defined. */
	spacing = spacing < 0 ? -((-spacing) / SPACING_DIVISOR)
			: spacing / SPACING_DIVISOR;

	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);
	rows = (struct mh_menu_sprite_row *)malloc(
		(unsigned long)ROW_CAPACITY * sizeof(*rows));

	if (work.pixels == NULL || rows == NULL) {
		free(work.pixels);
		free(rows);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing Credits");
	}

	next = *state;
	seed = *random;
	phase = *noise;
	memcpy(work.pixels, canvas->pixels, work.size);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		status = mh_menu_sprite_select(layout, CREDITS_LAYOUT, i, mix,
			0, 0, 0, &next.sprites, &seed, &commands, error_out);

		if (status != MH_ERROR_NONE || art->images[i].pixels == NULL)
			continue;

		status = mh_menu_sprite_draw_commands(&art->images[i],
			&commands, rows, ROW_CAPACITY, &phase, &work, 0,
			error_out);
	}

	bank = (unsigned int)mh_menu_credits_alternate(club, input);

	if (status == MH_ERROR_NONE)
		status = draw_text(font, bank, x, y, spacing, &work, error_out);

	if (status == MH_ERROR_NONE) {
		memcpy(canvas->pixels, work.pixels, work.size);
		*state = next;
		*random = seed;
		*noise = phase;
	}

	free(work.pixels);
	free(rows);

	return status;
}
