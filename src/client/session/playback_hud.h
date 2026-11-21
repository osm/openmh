#ifndef MH_CLIENT_SESSION_PLAYBACK_HUD_H
#define MH_CLIENT_SESSION_PLAYBACK_HUD_H

#include "client/session/demo_recording.h"
#include "engine/core/error.h"
#include "engine/ui/hud.h"

struct mh_playback_hud {
	struct mh_messages	messages;
	double			clock_ms, clock_offset_ms;
	unsigned long		last_ticks, status_flags;
	unsigned int		delivered_notices;
	int			has_messages;
};

void	mh_playback_hud_init(struct mh_playback_hud *hud);

/*
 * Retain history and clear delivered events after a successful restart.
 * Sampled clocks retain lifetime time; legacy tick diagnostics use offset.
 */
void	mh_playback_hud_restart(struct mh_playback_hud *hud,
	const struct mh_field *field);

/*
 * Borrow the already-loaded wrap font and original producer states. Collects
 * start/end/record/write-error notices once, including CLI pre-simulation.
 * No simulation, file, backend clock or audio changes. Atomic on failure.
 */
enum mh_error_code	mh_playback_hud_frame(struct mh_playback_hud *hud,
	const struct mh_hud_assets *assets, unsigned int width,
	const struct mh_field *field, const struct mh_demo_race *race,
	const struct mh_demo_recording *recording, struct mh_error *error_out);
void			mh_playback_hud_state(const struct mh_playback_hud *hud,
	struct mh_hud_state *state);

/* Local screenshot/chat notices share the recording/playback history. */
enum mh_error_code	mh_playback_hud_message(struct mh_playback_hud *hud,
	const struct mh_hud_assets *assets, const char *text,
	const char *suffix, unsigned long color, struct mh_error *error_out);

#endif
