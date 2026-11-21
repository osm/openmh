#include "client/menus/internal.h"
#include "engine/assets/path_directories.h"
#include "engine/core/error.h"
#include "engine/core/text.h"

static void
recover_save_failure(struct mh_menus *menus, struct mh_error error)
{
	fprintf(stderr, "Recording save failed: %s\n", error.message);

	menus->saving.phase = MH_MENU_RECORDING_NAME;
	menus->saving.pending_probe = 0;
	menus->saving.answer = 0;
	mh_menus_show_error(menus, error);
}

static void
resume_after_recording(struct mh_menus *menus)
{
	mh_demo_file_abort(menus->take);
	menus->take = NULL;
	mh_menus_request(&menus->menu, menus->save_return);

	if (menus->save_return ==
		&menus->league_page[MH_MENUS_LEAGUE_STANDINGS])
		puts("Frontend resumed: standings");
	else
		puts("Frontend resumed: main");

	fflush(stdout);
}

static void
apply_save_events(
	struct mh_menus *menus, const struct mh_menu_recording_output *output)
{
	struct mh_menu_recording_output next;
	struct mh_error error;
	unsigned int i;
	int exists;

	for (i = 0; menus->error.code == MH_ERROR_NONE && i < output->count;
		i++) {
		if (output->events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output->events[i].value);
		else if (output->events[i].kind == MH_MENU_DIALOG)
			mh_menus_open_dialog(menus, output->events[i].value);
		else if (output->events[i].kind == MH_MENU_RECORDING_PROBE) {
			if (mh_demo_file_probe(menus->take, menus->asset_root,
				menus->saving.name, &exists,
				&error) != MH_ERROR_NONE) {
				recover_save_failure(menus, error);
				return;
			}

			/*
			 * finish_probe cannot emit another PROBE; this is one
			 * continuation.
			 */
			if (mh_menu_recording_finish_probe(&menus->saving,
				exists, &next,
				&menus->error) == MH_ERROR_NONE)
				apply_save_events(menus, &next);
		} else if (output->events[i].kind ==
			MH_MENU_RECORDING_PUBLISH) {
			if (mh_demo_file_save(menus->take,
				(int)output->events[i].value,
				&error) != MH_ERROR_NONE) {
				recover_save_failure(menus, error);
				return;
			}

			printf("Recording saved: %s.MDE\n", menus->saving.name);
			fflush(stdout);
		} else if (output->events[i].kind == MH_MENU_RECORDING_RETURN) {
			resume_after_recording(menus);
		} else if (output->events[i].kind == MH_MENU_RECORDING_REFRESH)
			(void)mh_menus_recordings_reload(
				menus->asset_root, menus, &menus->error);
	}
}

void
mh_menus_save_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_recording_output output;
	unsigned int previous_phase;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	previous_phase = menus->saving.phase;
	menus->saving_input.modal_visible = menu->modal;
	menus->saving_input.clock_ms = menus->clock_ms;

	if (mh_menu_recording_update(&menus->saving, &menus->saving_input,
		menus->font.advance, &output,
		&menus->error) != MH_ERROR_NONE)
		return;

	if (mh_menu_recording_render(&output, &menus->font, &menus->work,
		&menus->error) != MH_ERROR_NONE)
		return;

	apply_save_events(menus, &output);

	if (menus->error.code == MH_ERROR_NONE &&
		menus->saving.phase == MH_MENU_RECORDING_NAME &&
		previous_phase != MH_MENU_RECORDING_NAME) {
		puts("Recording name entry");
		fflush(stdout);
	}
}

enum mh_error_code
mh_menus_recordings_reload(const char *asset_root, struct mh_menus *menus,
	struct mh_error *error_out)
{
	struct mh_recording_catalog catalog;
	char installed_directory[MH_MENUS_PATH_BYTES];
	enum mh_error_code status;
	unsigned int i, previous;
	int found, retain, same;

	previous = menus->recordings.menu.selected;
	retain = previous < menus->recordings.menu.count &&
		mh_ascii_equal(menus->recordings.menu.names[previous],
			menus->demo_name);

	status = mh_asset_path_find_from(menus->resources, asset_root,
		MH_ASSET_DEMOS_DIR, installed_directory,
		sizeof(installed_directory), &found, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_recording_catalog_read_from(menus->resources,
			menus->user_data, asset_root,
			found ? installed_directory : NULL,
			menus->recording_directory[0] == 0
				? NULL
				: menus->recording_directory,
			menus->demo_name, &catalog, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Equal path strings do not imply equal provider namespaces. */
	for (i = 0; retain && i < catalog.menu.count; i++) {
		status = mh_recording_catalog_same_entry(&menus->recordings,
			previous, &catalog, i, &same, error_out);

		if (status != MH_ERROR_NONE) {
			mh_recording_catalog_close(&catalog);
			return status;
		}

		if (same) {
			catalog.menu.selected = i;
			break;
		}
	}

	mh_recording_catalog_close(&menus->recordings);
	menus->recordings = catalog;

	for (i = 0; i < catalog.menu.count; i++)
		menus->demo_names[i] = catalog.menu.names[i];

	if (catalog.menu.count != 0)
		strcpy(menus->demo_name,
			catalog.menu.names[catalog.menu.selected]);
	else
		menus->demo_name[0] = 0;

	return status;
}
