#ifndef MH_DEMO_GHOST_CAPTURE_H
#define MH_DEMO_GHOST_CAPTURE_H

#include "engine/core/error.h"
#include "engine/demo/capture.h"
#include "engine/demo/race.h"

/*
 * Ghost mode (3): human at zero, up to seven 2.5 or twelve 3.0 recordings.
 * Preserve the source format; 3.0 also saves recorded and human horn names.
 * Header preserves recorded identity bytes, colors and device/gear words;
 * only the live human uses the supplied profile. Begin before countdown with
 * a dense active roster. Output is owned metadata, without borrowed frames.
 */
enum mh_error_code	mh_ghost_capture_header(const struct mh_demo_race *race,
	const struct mh_profile *human, struct mh_demo *header,
	struct mh_error *error_out);

/*
 * Encode remote effect/position channels from the playback cache, angles
 * from the applied matrix and throttle from the retained runtime pedal,
 * never local force/contact bookkeeping. The human uses the live vehicle
 * encoder. All initially present bodies remain encoded after departures.
 * Outputs are atomic.
 */
enum mh_error_code	mh_ghost_capture_sample(const struct mh_demo_race *race,
	const struct mh_demo *header, struct mh_demo_record_sample *sample,
	struct mh_error *error_out);
enum mh_error_code	mh_ghost_capture_crossings(
	const struct mh_demo_race *race,
	struct mh_demo_player players[MH_DEMO_MAX_PLAYERS],
	struct mh_error *error_out);

/*
 * Same atomic capture boundary as local recording, after recorded poses
 * apply and before wheels/recovery/progress. No allocation or file IO.
 */
enum mh_error_code	mh_ghost_capture_advance(struct mh_demo_race *race,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, const struct mh_demo *header,
	struct mh_demo_capture_frame *capture, unsigned int *steps,
	unsigned long *random, struct mh_error *error_out);

#endif
