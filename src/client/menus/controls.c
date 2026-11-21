#include "client/menus/internal.h"
#include "client/settings/controls.h"
#include "engine/core/error.h"
#include "engine/core/text.h"

enum mh_error_code
mh_menus_controls_load(struct mh_menus *menus, const char *directory,
	int persist, struct mh_error *error_out)
{
	enum mh_control_storage storage;
	enum mh_error_code status;

	storage = persist && !menus->capture_only ? MH_CONTROL_PERSISTENT
						: MH_CONTROL_SESSION;
	status = mh_control_profiles_open(menus->resources, menus->user_data,
		menus->asset_root, directory, storage,
		menus->settings->control_name, &menus->controls_catalog,
		&menus->controls_store, error_out);

	if (status == MH_ERROR_NONE) {
		menus->controls.selected = menus->controls_catalog.selected;
		menus->controls_persistent =
			directory != NULL || storage == MH_CONTROL_PERSISTENT;
	}

	return status;
}

enum mh_error_code
mh_menus_controls_reload(struct mh_menus *menus, struct mh_error *error_out)
{
	const struct mh_control_layout *layout;
	unsigned int i;
	enum mh_error_code status;

	if (menus->controls_persistent) {
		/* Reload the catalog after CFG, selecting its saved name. */
		status = mh_control_store_reload(&menus->controls_store,
			menus->settings->control_name, &menus->controls_catalog,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else {
		/* Explicit session-only edits have no saved file to reload. */
		menus->controls_catalog.selected = 0;

		for (i = 0; i < menus->controls_catalog.count; i++)
			if (mh_ascii_equal(menus->controls_catalog.profiles[i]
						.layout.name,
				menus->settings->control_name))
				menus->controls_catalog.selected = i;
	}

	status = mh_control_profiles_restrict(
		menus->controls_store, &menus->controls_catalog, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	menus->controls.selected = menus->controls_catalog.selected;

	layout = &menus->controls_catalog.profiles[menus->controls.selected]
			.layout;

	return mh_control_layout_apply(
		layout, &menus->applied_controls, error_out);
}

enum mh_error_code
mh_menus_controls_save(struct mh_menus *menus, struct mh_error *error_out)
{
	const struct mh_control_layout *layout;
	enum mh_error_code status;

	if (menus->controls.selected >= menus->controls_catalog.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid selected control profile");

	menus->controls_catalog.selected = menus->controls.selected;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!menus->capture_only)
		status = mh_control_store_save(menus->controls_store,
			&menus->controls_catalog, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	layout = &menus->controls_catalog.profiles[menus->controls.selected]
			.layout;
	status = mh_control_layout_apply(
		layout, &menus->applied_controls, error_out);

	if (status == MH_ERROR_NONE)
		strcpy(menus->settings->control_name, layout->name);

	return status;
}

void
mh_menus_controls_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->controls_entries++;

	if (menus->controls.preferences)
		menus->controls.row = mh_menu_choice_first(
			&menus->controls_choices, menus->controls.row);

	mh_menu_controls_animation_init(&menus->controls_animation);
}

void
mh_menus_controls_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	menus = (struct mh_menus *)context;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_controls_render(&menus->controls_art,
			&menus->font, &menus->layout, &menus->controls,
			menus->controls_catalog.profiles,
			menus->controls_catalog.count, NULL, menu->position_mix,
			menus->clock_ms, &menus->controls_animation,
			&menus->random_state, &menus->row_noise, &menus->work,
			menus->controls.preferences ? &menus->controls_choices
						: NULL,
			&menus->error);
}

void
mh_menus_controls_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_controls_output output;
	unsigned int i;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	menus->controls_keys.clock_ms = menus->clock_ms;
	menus->controls_keys.elapsed_ms = menu->elapsed_ms;

	if (mh_menu_controls_update(&menus->controls,
		menus->controls_catalog.profiles,
		menus->controls_catalog.count, &menus->controls_keys,
		&output,
		menus->controls.preferences ? &menus->controls_choices
						: NULL,
		&menus->error) != MH_ERROR_NONE)
		return;

	for (i = 0; menus->error.code == MH_ERROR_NONE && i < output.count;
		i++) {
		if (output.events[i].kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, output.events[i].value);
		else if (output.events[i].kind == MH_MENU_PAGE &&
			output.events[i].value == MH_MENU_OPTIONS)
			mh_menus_request(menu, &menus->options_page);
		else if (output.events[i].kind == MH_MENU_CONTROLS_INPUT_MODE)
			mh_menus_set_input_mode(menus, output.events[i].value);
	}
}
