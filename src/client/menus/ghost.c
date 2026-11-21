#include "client/menus/internal.h"
#include "engine/core/error.h"

void
mh_menus_ghost_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->ghost_entries++;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menus_recordings_reload(
			menus->asset_root, menus, &menus->error);

	menus->ghost.count = menus->recordings.menu.count;
	menus->ghost.selected = menus->recordings.menu.selected;
	menus->ghost.mode = menus->single.mode;
	menus->ghost.patched = menus->ghost_art.patched;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_ghost_enter(&menus->ghost, &menus->error);

	if (menus->ghost_entries == 1)
		menus->ghost.focus = menus->ghost_initial_focus;

	if (menus->ghost.count == 0 &&
		menus->ghost.focus == mh_menu_ghost_list_focus(&menus->ghost))
		menus->ghost.focus = MH_MENU_GHOST_FOCUS_RACE;

	mh_menu_ghost_animation_init(&menus->ghost_animation);

	if (menus->error.code == MH_ERROR_NONE)
		mh_menus_trace_frames(
			menus, "ghost-entry", menus->ghost_animation.frames);
}

void
mh_menus_ghost_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_ghost_render(&menus->ghost_art, &menus->font,
			&menus->layout, &menus->ghost, menus->demo_names,
			menu->position_mix, menus->clock_ms, menu->elapsed_ms,
			&menus->ghost_animation, &menus->random_state,
			&menus->row_noise, &menus->work, &menus->error);

	/*
	 * Keep the Single-player summary inside its four-entry label table.
	 * Demo's runtime mode 6 belongs to the playback launch, not that table.
	 */
	if (menus->error.code == MH_ERROR_NONE)
		menus->single.mode = MH_GAME_GHOST;

	if (menus->error.code == MH_ERROR_NONE)
		mh_menus_trace_frames(
			menus, "ghost", menus->ghost_animation.frames);
}

static void
apply_ghost_events(struct mh_menus *menus,
	const struct mh_menu_ghost_output *output, struct mh_error *failure)
{
	struct mh_error error;
	unsigned int i, selected;

	for (i = 0; menus->error.code == MH_ERROR_NONE && i < output->count;
		i++) {
		if (output->events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output->events[i].value);
		else if (output->events[i].kind == MH_MENU_GHOST_NAME) {
			selected = output->events[i].value;
			menus->recordings.menu.selected = selected;
			strcpy(menus->demo_name,
				menus->recordings.menu.names[selected]);
		} else if (output->events[i].kind == MH_MENU_GHOST_DELETE) {
			if (mh_recording_catalog_remove(&menus->recordings,
				output->events[i].value,
				&error) != MH_ERROR_NONE)
				*failure = error;
		} else if (output->events[i].kind == MH_MENU_GHOST_REFRESH) {
			if (mh_menus_recordings_reload(menus->asset_root, menus,
				&error) != MH_ERROR_NONE)
				*failure = error;
		} else if (output->events[i].kind == MH_MENU_DIALOG)
			mh_menus_open_dialog(menus, output->events[i].value);
		else if (output->events[i].kind == MH_MENU_PAGE &&
			output->events[i].value == MH_MENU_SINGLE)
			mh_menus_request(&menus->menu, &menus->single_page);
		else if (output->events[i].kind == MH_MENU_PAGE &&
			output->events[i].value == MH_MENU_CAR_SETUP)
			mh_menus_request(&menus->menu, &menus->car_page);
		else if (output->events[i].kind == MH_MENU_PAGE &&
			output->events[i].value == MH_MENU_QUICK)
			mh_menus_request_launch(menus);
	}
}

static enum mh_error_code
guard_recording_delete(struct mh_menus *menus,
	struct mh_menu_ghost_input *input, int *read_only,
	struct mh_error *error_out)
{
	struct mh_menu_ghost_state probe;
	struct mh_menu_ghost_input navigation;
	struct mh_menu_ghost_output output;
	enum mh_error_code status;

	/*
	 * Original navigation precedes Delete. Probe that selection so combined
	 * Right/Down/Delete input cannot pass an installed entry to deletion.
	 */
	probe = menus->ghost;
	probe.delete_answer = 0;
	navigation = *input;
	navigation.remove = 0;
	navigation.keys.enter = navigation.keys.escape = 0;
	status = mh_menu_ghost_update(&probe, &navigation, &output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*read_only = probe.focus == mh_menu_ghost_list_focus(&probe) &&
		probe.count != 0 &&
		!mh_recording_catalog_removable(
			&menus->recordings, probe.selected);

	if (*read_only) {
		input->remove = 0;
		input->keys.enter = input->keys.escape = 0;
		menus->ghost.delete_answer = 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_menus_ghost_input(struct mh_menu *menu, void *context)
{
	static const unsigned char protected_files[] =
		"Recording is read-only.\0";
	static const unsigned char failed[] =
		"Could not change recording list.\0Please select the recording again.\0";
	struct mh_menus *menus;
	struct mh_menu_ghost_input input;
	struct mh_menu_ghost_output output;
	struct mh_error failure;
	int read_only;

	(void)menu;
	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	input = menus->ghost_input;
	failure = mh_success;
	read_only = 0;

	if (input.remove == 1 || menus->ghost.delete_answer == 1) {
		if (guard_recording_delete(menus, &input, &read_only,
			&menus->error) != MH_ERROR_NONE)
			return;
	}

	if (mh_menu_ghost_update(&menus->ghost, &input, &output,
		&menus->error) != MH_ERROR_NONE)
		return;

	apply_ghost_events(menus, &output, &failure);

	if (menus->error.code == MH_ERROR_NONE && menus->ghost.pending_delete) {
		if (mh_menu_ghost_finish_delete(&menus->ghost,
			menus->recordings.menu.count,
			menus->recordings.menu.selected, &input, &output,
			&menus->error) == MH_ERROR_NONE)
			apply_ghost_events(menus, &output, &failure);
	}

	if (menus->error.code == MH_ERROR_NONE && read_only)
		mh_menus_show_notice(
			menus, protected_files, sizeof(protected_files));
	else if (menus->error.code == MH_ERROR_NONE &&
		failure.code != MH_ERROR_NONE) {
		fprintf(stderr, "Recording catalog: %s\n", failure.message);
		mh_menus_show_notice(menus, failed, sizeof(failed));
	}
}
