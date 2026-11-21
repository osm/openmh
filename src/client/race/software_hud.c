#include "client/race/internal.h"

static const char *
status_text(const struct mh_hud_state *state)
{
	if (state->message != NULL)
		return state->message;

	if (state->paused)
		return "PAUSED";

	if (state->finished)
		return state->complete ? "RESULTS - SHIFT+TAB"
				: "FINISHED - SHIFT+TAB";

	if (state->start == MH_START_READY)
		return "READY";

	return NULL;
}

int
mh_race_build_software_overlay(struct mh_race_loop *loop)
{
	const struct mh_software_hud_assets *assets;
	struct mh_software_hud_secrets secrets;
	const char *message;
	enum mh_error_code status;

	assets = &loop->context->software_hud;
	secrets = loop->history->software_hud;

	if (loop->menu.visible)
		status = mh_software_hud_pause(assets, &loop->menu,
			&loop->menu_output.display, loop->menu_input.mode,
			loop->live && loop->session->network_host, &loop->ui,
			&loop->error);
	else
		status = mh_software_hud_instruments(assets, &loop->hud,
			loop->context->scene.assets.track.name, &loop->ui,
			&secrets, &loop->error);

	/* Shared notice precedes notification text. */
	if (status == MH_ERROR_NONE && !loop->menu.visible &&
		!loop->hud.show_info && loop->field->attract &&
		loop->attract_frame.label)
		status = mh_software_hud_label(
			assets, "DEMO", 4, &loop->ui, &loop->error);

	if (status == MH_ERROR_NONE)
		status = mh_software_hud_messages(
			assets, &loop->hud, &loop->ui, &loop->error);

	if (status == MH_ERROR_NONE)
		status = mh_software_hud_chat(assets, loop->hud.chat_text,
			loop->hud.chat_ms, &loop->ui, &loop->error);

	if (status == MH_ERROR_NONE && !loop->menu.visible &&
		!loop->hud.show_info) {
		message = status_text(&loop->hud);

		if (message != NULL)
			status = mh_software_hud_label(assets, message,
				(int)(loop->height / 2), &loop->ui,
				&loop->error);
		else
			status = mh_software_hud_countdown(assets,
				loop->hud.start, &loop->ui, &loop->error);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Software HUD: %s\n", loop->error.message);
		loop->success = loop->running = 0;
		return 0;
	}

	loop->history->software_hud = secrets;

	return 1;
}
