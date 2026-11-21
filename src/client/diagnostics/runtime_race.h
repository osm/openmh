#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_RACE_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_RACE_H

#include "client/race/race.h"

struct mh_session_loading;

/*
 * Fill native services, open the race, retire loading, then run to completion.
 */
int	mh_diagnostic_race_run(struct mh_race_context *context,
	struct mh_field *field, struct mh_race_loop_options *options,
	struct mh_session_loading *loading);

/* Capture renders the supplied state once; NULL capture runs interactively. */
int	mh_race_run(struct mh_race_context *context, struct mh_field *field,
	const char *capture, int headless, int results, const char *root,
	const struct mh_audio_options *audio_options, FILE *trace,
	const struct mh_debug_options *debug);

int	mh_race_run_session(struct mh_race_context *context,
	struct mh_field *field, const char *root,
	const struct mh_audio_options *audio_options, FILE *trace,
	const struct mh_race_session_hooks *session,
	const struct mh_debug_options *debug);

/*
 * One deterministic frame from an already advanced session. Uses its HUD
 * callback without calling advance/restart or advancing any session clock.
 */
int	mh_race_capture_session(struct mh_race_context *context,
	struct mh_field *field, const char *capture, int headless,
	const char *root, const struct mh_audio_options *audio_options,
	const struct mh_race_session_hooks *session,
	const struct mh_debug_options *debug);

/*
 * Local frontend handoff with optional recording callbacks and finalized
 * result acceptance. An optional session must pause with the local UI.
 * quit distinguishes closing the race window from returning through its UI;
 * completed distinguishes accepted results. Both start at zero. The caller
 * retains any separately initialized frontend resources. Optional music
 * output starts NULL and receives owned background results audio only on
 * successful acceptance. Close it on every caller exit path.
 */
int	mh_race_run_local(struct mh_race_context *context,
	struct mh_field *field, const char *root,
	const struct mh_audio_options *audio_options,
	const struct mh_race_session_hooks *session,
	const struct mh_race_results_handler *results, int *completed,
	int *quit, struct mh_audio **music,
	const struct mh_debug_options *debug);

/*
 * Mode 5 has no result/record award. Raw Enter/Escape returns manual_exit=1;
 * timed return leaves it zero. Window close sets quit. Optional callbacks
 * retain the ordinary local frame contract, e.g. for supplied-clock checks.
 */
int	mh_race_run_attract(struct mh_race_context *context,
	struct mh_field *field, const char *root,
	const struct mh_audio_options *audio_options,
	const struct mh_race_session_hooks *session, int *manual_exit,
	int *quit, const struct mh_debug_options *debug);

#endif
