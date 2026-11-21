#include "client/menus/internal.h"
#include "engine/core/error.h"
#include "engine/ui/menu_dialog_draw.h"
#include "engine/ui/menu_modal.h"

enum {
	ERROR_TEXT_LINES = 4,
	ERROR_TEXT_WIDTH = 208
};

void
mh_menus_open_dialog(struct mh_menus *menus, unsigned int kind)
{
	unsigned int initial;
	enum mh_error_code status;

	status = mh_menu_dialog_builtin(
		kind, &menus->dialog_text, &menus->error);
	initial = kind == MH_MENU_CONFIRM_EXIT ||
			kind == MH_MENU_CONFIRM_DEMO_DELETE ||
			kind == MH_MENU_CONFIRM_LEAGUE_DELETE ||
			kind == MH_MENU_CONFIRM_ADDRESS_DELETE
		? 2
		: 1;

	if (status == MH_ERROR_NONE)
		status = mh_menu_modal_open(&menus->modal,
			menus->dialog_text.choice_count, initial,
			kind == MH_MENU_CONFIRM_LEAGUE_CREATE ? 2 : initial,
			&menus->error);

	if (status == MH_ERROR_NONE) {
		menus->menu.modal = 1;
		menus->dialog_kind = kind;
		mh_menus_sound_event(menus, MH_MENU_SOUND_PAGE_IN);

		if (kind == MH_MENU_CONFIRM_RECORD_SAVE ||
			kind == MH_MENU_CONFIRM_RECORD_OVERWRITE) {
			printf("Recording dialog: %s\n",
				kind == MH_MENU_CONFIRM_RECORD_SAVE
					? "save"
					: "overwrite");
			fflush(stdout);
		}
	}
}

void
mh_menus_show_notice(
	struct mh_menus *menus, const unsigned char *title, size_t size)
{
	static const unsigned char choices[] = "OK\0";
	enum mh_error_code status;

	status = mh_menu_dialog_text_read(title, size, choices, sizeof(choices),
		&menus->dialog_text, &menus->error);

	if (status == MH_ERROR_NONE)
		status = mh_menu_modal_open(
			&menus->modal, 1, 1, 1, &menus->error);

	if (status == MH_ERROR_NONE) {
		menus->menu.modal = 1;
		menus->dialog_kind = 0;
		mh_menus_sound_event(menus, MH_MENU_SOUND_PAGE_IN);
	}
}

void
mh_menus_show_error(struct mh_menus *menus, struct mh_error error)
{
	unsigned char title[MH_MENU_DIALOG_TEXT_BYTES];
	size_t source, used, line_start, last_space;
	unsigned int width, lines, glyph;

	if (mh_menus_compact(menus)) {
		mh_error_copy(&menus->error, &error, menus->compact_notice,
			sizeof(menus->compact_notice));
		menus->error = mh_success;
		return;
	}

	source = used = lines = 0;

	/*
	 * Native errors fit the original requester's text panel. Original
	 * NUL-separated dialog strings continue through notice unchanged.
	 */
	while (error.message[source] != 0 && lines < ERROR_TEXT_LINES &&
		used < sizeof(title) - 2) {
		while (error.message[source] == ' ')
			source++;

		if (error.message[source] == 0)
			break;

		line_start = used;
		last_space = (size_t)-1;
		width = 0;

		while (error.message[source] != 0 && used < sizeof(title) - 2) {
			glyph = (unsigned char)error.message[source];

			if (width + menus->font.advance[glyph] >
				ERROR_TEXT_WIDTH)
				break;

			if (glyph == ' ')
				last_space = used;

			title[used++] = (unsigned char)glyph;
			width += menus->font.advance[glyph];
			source++;
		}

		if (error.message[source] != 0 &&
			error.message[source] != ' ' &&
			last_space != (size_t)-1) {
			source -= used - last_space;
			used = last_space;
		}

		if (used == line_start)
			break;

		title[used++] = 0;
		lines++;
	}

	title[used++] = 0;
	mh_menus_show_notice(menus, title, used);
}

static void
apply_dialog_answer(struct mh_menus *menus, unsigned long answer)
{
	if (menus->dialog_kind == MH_MENU_CONFIRM_EXIT)
		menus->main.exit_answer = answer;

	if (menus->dialog_kind == MH_MENU_CONFIRM_DEMO_DELETE)
		menus->ghost.delete_answer = answer;

	if (menus->dialog_kind == MH_MENU_CONFIRM_LEAGUE_CREATE)
		menus->league.create_answer = answer;

	if (menus->dialog_kind == MH_MENU_CONFIRM_LEAGUE_DELETE)
		menus->league.delete_answer = answer;

	if (menus->dialog_kind == MH_MENU_CONFIRM_ADDRESS_DELETE)
		menus->multi.delete_answer = (unsigned int)answer;

	if (menus->dialog_kind == MH_MENU_CONFIRM_RECORD_SAVE ||
		menus->dialog_kind == MH_MENU_CONFIRM_RECORD_OVERWRITE)
		menus->saving.answer = answer;
}

void
mh_menus_dialog_step(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_modal_state next;
	struct mh_menu_modal_output output;
	unsigned int i;
	enum mh_error_code status;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	next = menus->modal;
	status = mh_menu_modal_step(&next, menu->elapsed_ms,
		&menus->navigation_keys, &output, &menus->error);

	if (status == MH_ERROR_NONE)
		status = mh_menu_dialog_render(&menus->dialog_text,
			&menus->font, &menus->dialog_sprite,
			menus->static_noise, output.display_x,
			output.display_selected, &menus->random_state,
			&menus->work, &menus->error);

	if (status != MH_ERROR_NONE)
		return;

	menus->modal = next;
	menu->modal = next.visible;

	apply_dialog_answer(menus, next.answer);

	for (i = 0; i < output.count; i++)
		mh_menus_sound_event(menus, output.sounds[i]);
}
