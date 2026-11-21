#include <string.h>

#include "client/race/internal.h"
#include "engine/core/error.h"

static void
apply_info_metadata(struct mh_race_loop *loop)
{
	unsigned int i, entry;

	for (i = 0; i < loop->hud.info.count; i++) {
		struct mh_race_info_row *row;
		const struct mh_race_info_metadata *metadata;

		row = &loop->hud.info.rows[i];
		entry = row->entry;

		if (loop->context->info_metadata_mask & (1U << entry)) {
			metadata = &loop->context->info_metadata[entry];
			row->automatic = metadata->automatic;
			row->control_type = metadata->control_type;
			memcpy(row->name, metadata->name,
				sizeof(metadata->name) - 1);
			row->name[sizeof(metadata->name) - 1] = 0;
			memcpy(row->team, metadata->team,
				sizeof(metadata->team) - 1);
			row->team[sizeof(metadata->team) - 1] = 0;
		} else if (entry == 0 &&
			loop->field->cars[entry].ai_path == NULL)
			row->control_type = loop->context->controls == NULL
				? MH_CONTROL_KEYBOARD
				: mh_control_type_update(
					loop->context->controls
						->codes[MH_CONTROL_LEFT],
					0);
	}
}

static enum mh_error_code
prepare_messages(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int font_height;

	if (loop->local_messages_present && loop->hud.messages == NULL) {
		status = mh_clock_word(mh_drive_clock_ms(&loop->field->cars[0]),
			&loop->screenshot_clock, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		loop->hud.messages = &loop->local_messages;
		loop->hud.message_ms = (double)loop->screenshot_clock;
	}

	font_height = loop->context->scene.assets.profile == MH_RENDER_SOFTWARE
		? loop->context->software_hud.font.height
		: loop->context->hud.hardware_height;

	if (loop->hud.messages != NULL) {
		status = mh_messages_view(loop->hud.messages,
			loop->hud.message_ms, loop->height, font_height,
			&loop->messages, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_race_prepare_hud(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_scene_assets *assets;
	enum mh_error_code status;

	scene = &loop->context->scene;
	assets = &scene->assets;

	status = mh_hud_state(
		loop->field, loop->paused, &loop->hud, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "HUD state: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	loop->hud.show_info = loop->info_held != 0 && !loop->menu.visible;

	if (loop->hud.show_info) {
		status = mh_race_info_state(loop->field,
			loop->context->results.count == loop->field->count
				? loop->context->results.profiles
				: NULL,
			NULL, &loop->hud.info, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Info roster: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		apply_info_metadata(loop);
	}

	loop->hud.map_mode = loop->map_mode;
	loop->hud.gap_mode = loop->gap_mode;
	loop->hud.checkpoint_info = loop->gap_mode;

	if (loop->context->settings != NULL) {
		loop->hud.info_detail = loop->context->settings->info_detail;
		loop->hud.system = loop->context->settings->system;
		loop->hud.speed = mh_hud_display_speed(
			(float)loop->field->cars[mh_field_viewed(loop->field)]
				.vehicle.speed,
			loop->hud.system);
	}

	mh_hud_map_state(loop->field, loop->models,
		loop->context->results.count == loop->field->count
			? loop->context->results.profiles
			: NULL,
		&loop->hud);
	status = mh_hud_gap_state(loop->field,
		loop->context->results.count == loop->field->count
			? loop->context->results.profiles
			: NULL,
		&loop->hud, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Race gaps: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	if (loop->continuous) {
		loop->hud.paused = 0;

		if (loop->paused || !loop->focused)
			loop->hud.message = "INPUT RELEASED - RACE CONTINUES";
	}

	if (loop->session != NULL && loop->session->hud != NULL)
		loop->session->hud(loop->session->context, &loop->hud);

	status = prepare_messages(loop, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Messages: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	if (assets->profile != MH_RENDER_SOFTWARE && !loop->hud.show_info)
		mh_hud_secrets_frame(&loop->history->hud, assets->track.name,
			&loop->hud, loop->width,
			loop->context->hud.secret_scroll_length);

	return 1;
}

int
mh_race_build_overlay(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	mh_ui_begin(&loop->ui, loop->width, loop->height);

	if (MH_RENDER_BUILD_SOFTWARE &&
		scene->assets.profile == MH_RENDER_SOFTWARE)
		return mh_race_build_software_overlay(loop) &&
			mh_race_frame_rate_draw(loop);

	if (loop->tron) {
		status = mh_tron_intro_frame(loop->width, loop->height,
			visuals->tron_time, loop->effect_dt, 1,
			&loop->tron_intro, &loop->error);

		if (status != MH_ERROR_NONE ||
			!mh_tron_intro_draw(&loop->context->hud,
				&loop->tron_intro, &loop->ui)) {
			fprintf(stderr, "TRON introduction: %s\n",
				status != MH_ERROR_NONE ? loop->error.message
							: "layout failed");
			loop->success = 0;
			loop->running = 0;
			return 0;
		}
	}

	/*
	 * Flush the scene (including TRON's intro), then call the renderer's
	 * HUD callback.
	 */
	if (!loop->menu.visible &&
		!mh_hud_append(&loop->context->hud, &loop->hud, loop->width,
			loop->height, &loop->ui))
		goto layout_failed;

	if (loop->menu.visible &&
		!mh_pause_draw(&loop->context->hud, &loop->menu,
			&loop->menu_output.display, loop->menu_input.mode,
			loop->live && loop->session->network_host, &loop->ui))
		goto layout_failed;

	if (loop->field->attract && !loop->menu.visible &&
		!loop->hud.show_info &&
		!mh_attract_label_draw(&loop->context->hud,
			loop->attract_frame.label, &loop->ui))
		goto layout_failed;

	if (loop->menu.visible &&
		(!mh_hud_messages_draw(
			&loop->context->hud, &loop->hud, &loop->ui) ||
			!mh_chat_draw(&loop->context->hud, loop->hud.chat_text,
				loop->hud.chat_ms, &loop->ui)))
		goto layout_failed;

	return mh_race_frame_rate_draw(loop);

layout_failed:
	fprintf(stderr, "HUD layout failed\n");
	loop->success = 0;
	loop->running = 0;

	return 0;
}
