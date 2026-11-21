#include <string.h>

#include "client/menus/internal.h"
#include "client/race/race.h"
#include "engine/core/error.h"
#include "engine/ui/menu_position.h"

enum {
	CREDITS_LAYOUT = 18,
	TEXT_ORIGIN_SLOT = 24,
	TEXT_SPACING_SLOT = 25,
	SPACING_DIVISOR = 8,
	ROW_HEIGHT = 11,
	OPENMH_HEADING_ROW = 20,
	OPENMH_AUTHOR_ROW = 22,
	HEADING_COLOR = 3,
	TEXT_COLOR = 1
};

static enum mh_error_code
draw_credit_line(struct mh_menus *menus, const char *text, long x, long y,
	unsigned int color, struct mh_error *error_out)
{
	const unsigned char *line;
	unsigned int i, width;

	line = (const unsigned char *)text;
	width = 0;

	for (i = 0; line[i] != 0; i++)
		width += menus->font.advance[line[i]];

	return mh_menu_text_draw(&menus->font, line, strlen(text) + 1,
		x - (long)(width / 2), y, color, &menus->work, error_out);
}

static enum mh_error_code
draw_openmh_credit(struct mh_menus *menus, unsigned int mix, int alternate,
	struct mh_error *error_out)
{
	const char *author;
	long x, y, unused, spacing;
	enum mh_error_code status;

	status = mh_menu_position(&menus->layout, CREDITS_LAYOUT,
		TEXT_ORIGIN_SLOT, mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(&menus->layout, CREDITS_LAYOUT,
			TEXT_SPACING_SLOT, mix, &unused, &spacing, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	spacing /= SPACING_DIVISOR;
	author = alternate ? "github.com/osm" : "Oscar Linderholm";
	status = draw_credit_line(menus, "OpenMH:", x,
		y + OPENMH_HEADING_ROW * spacing, HEADING_COLOR, error_out);

	if (status == MH_ERROR_NONE)
		status = draw_credit_line(menus, author, x,
			y + OPENMH_AUTHOR_ROW * spacing, TEXT_COLOR, error_out);

	return status;
}

void
mh_menus_credits_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	long *spacing;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->credits_entries++;
	menus->credits_bank = -1;

	/* Fit both credits sections above the original copyright artwork. */
	spacing = menus->layout.words[CREDITS_LAYOUT][TEXT_SPACING_SLOT];
	spacing[MH_MENU_LAYOUT_Y0] = ROW_HEIGHT * SPACING_DIVISOR;
	spacing[MH_MENU_LAYOUT_Y1] = 0;

	mh_menu_credits_animation_init(&menus->credits_animation);
}

void
mh_menus_credits_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	int club, bank;

	menus = (struct mh_menus *)context;
	club = mh_race_club_credits();
	bank = mh_menu_credits_alternate(club, &menus->credits_keys);

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_credits_render(&menus->credits_art, &menus->font,
			&menus->layout, club, &menus->credits_keys,
			menu->position_mix, &menus->credits_animation,
			&menus->random_state, &menus->row_noise, &menus->work,
			&menus->error);

	if (menus->error.code == MH_ERROR_NONE)
		(void)draw_openmh_credit(
			menus, menu->position_mix, bank, &menus->error);

	if (menus->error.code == MH_ERROR_NONE && bank != menus->credits_bank) {
		menus->credits_bank = bank;
		printf("Credits: entry %u, alternate %d, Club TBL %d\n",
			menus->credits_entries, bank, club);
		fflush(stdout);
	}
}

void
mh_menus_credits_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_output output;
	unsigned int i;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	if (mh_menu_credits_update(&menus->credits_keys, &output,
		&menus->error) != MH_ERROR_NONE)
		return;

	for (i = 0; menus->error.code == MH_ERROR_NONE && i < output.count;
		i++) {
		if (output.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_PAGE)
			mh_menus_request(menu, &menus->main_page);
	}
}
