#ifndef MH_SOFTWARE_HUD_H
#define MH_SOFTWARE_HUD_H

#include "engine/ui/hud.h"
#include "engine/ui/pause.h"
#include "engine/ui/software_hud_assets.h"

struct mh_software_hud_secrets {
	unsigned long	visits;
	int		club_credits;
};

/*
 * Append the original software instrument panel: gauges, race clocks,
 * lap/position ratios, time notices, wrong-way and status icons. Map,
 * countdown, gaps and text overlays are separate callers. Uses the loaded
 * resolution bank without stretching sprites. Failure restores count.
 */
enum mh_error_code	mh_software_hud_panel(
	const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list,
	struct mh_error *error_out);

/*
 * Software map, ranked sprites and colored font markers. Omits
 * singular/nonfinite/off-map projections. Preserves all list counts on failure,
 * including optional secret state. The viewed active entry updates the
 * RuhrStadt visit counter and draws its greeting before its own marker. NULL
 * secrets omits this side effect; the caller owns its process lifetime.
 */
enum mh_error_code	mh_software_hud_map(
	const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, const char *track,
	struct mh_software_hud_secrets *secrets, struct mh_ui_list *list,
	struct mh_error *error_out);

/*
 * Map and panel, then gaps; info mode replaces the map/panel with info after
 * the gaps.
 * Caller commits secret history only after the complete overlay succeeds.
 */
enum mh_error_code	mh_software_hud_instruments(
	const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, const char *track,
	struct mh_ui_list *list, struct mh_software_hud_secrets *secrets,
	struct mh_error *error_out);

/* One of the four count frames, centered at one eighth height. */
enum mh_error_code	mh_software_hud_countdown(
	const struct mh_software_hud_assets *assets, enum mh_start_phase phase,
	struct mh_ui_list *list, struct mh_error *error_out);

/* RGB packing; callers supply validated green_bits 5 or 6. */
unsigned int	mh_software_hud_color(unsigned long color,
	unsigned int green_bits);

/*
 * Word-font overlays append atomically, borrowing the decoded font until draw.
 */
enum mh_error_code	mh_software_hud_gaps(
	const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list,
	struct mh_error *error_out);
enum mh_error_code	mh_software_hud_messages(
	const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list,
	struct mh_error *error_out);
enum mh_error_code	mh_software_hud_chat(
	const struct mh_software_hud_assets *assets, const char *text,
	unsigned long milliseconds, struct mh_ui_list *list,
	struct mh_error *error_out);

/* Shared pause layout, using software word-font callbacks. */
enum mh_error_code	mh_software_hud_pause(
	const struct mh_software_hud_assets *assets,
	const struct mh_pause_state *state,
	const struct mh_pause_settings *display, unsigned int mode,
	int network_host, struct mh_ui_list *list, struct mh_error *error_out);

/* Shared original Info compositor, with the software word-font callbacks. */
enum mh_error_code	mh_software_hud_info(
	const struct mh_software_hud_assets *assets,
	const struct mh_race_info *info, struct mh_ui_list *list,
	struct mh_error *error_out);

/* Centered white text with a black shadow, for attract and native status. */
enum mh_error_code	mh_software_hud_label(
	const struct mh_software_hud_assets *assets, const char *text, int y,
	struct mh_ui_list *list, struct mh_error *error_out);

#endif
