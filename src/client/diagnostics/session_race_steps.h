#ifndef MH_CLIENT_DEBUG_RACE_STEPS_H
#define MH_CLIENT_DEBUG_RACE_STEPS_H

#include <stdio.h>

#include "engine/core/error.h"

struct mh_race_context;
struct mh_field;
struct mh_demo_recording;

/*
 * Fixed diagnostic ticks, separate from interactive frame timing. Borrow the
 * field, visual state, trace stream and optional recorder. The caller owns
 * cleanup and checks trace errors after diagnostics complete.
 */
enum mh_error_code	mh_debug_race_ticks(
	struct mh_race_context *race_context, struct mh_field *field,
	unsigned long ticks, FILE *trace, struct mh_demo_recording *recording,
	struct mh_error *error_out);

/*
 * Seven-number text input replay, not MDE playback. Owns only its input stream;
 * reports the failed line and returns the error to the launcher's cleanup.
 */
enum mh_error_code	mh_debug_race_replay(
	struct mh_race_context *race_context, struct mh_field *field,
	const char *path, FILE *trace, struct mh_demo_recording *recording,
	struct mh_error *error_out);

#endif
