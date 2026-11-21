#ifndef MH_DEMO_CAPTURE_H
#define MH_DEMO_CAPTURE_H

#include "engine/core/error.h"
#include "engine/demo/record.h"
#include "engine/simulation/field.h"
#include "interfaces/catalog/profile.h"

struct mh_demo_capture_frame {
	/* False on SUSPEND or before >6s; MENU frames can capture. */
	int				ready;
	unsigned long			clock_ms;
	struct mh_demo_record_sample	sample;
};

/*
 * New local takes use MDE 3.0, including the profiles' selected horns.
 * Fresh native local/AI race to original recording metadata. Names are
 * original-width nick/team/car keys, not filesystem paths. Header bit zero
 * records the catch-up setting. Device identity defaults to keyboard for
 * the human and unclassified for AI; applied profiles override the human
 * word in the recording adapter before publication.
 * No SDL/OS types, allocations or mutations of the running field.
 */
enum mh_error_code	mh_demo_capture_header(const struct mh_field *field,
	const struct mh_profile *profiles, struct mh_demo *header,
	struct mh_error *error_out);

/*
 * Capture every initially present local/AI entrant through the vehicle codec.
 * Native network/ghost recording needs its own roster/cached pose adapter; it
 * must not substitute locally simulated force/effect data.
 */
enum mh_error_code	mh_demo_capture_sample(const struct mh_field *field,
	const struct mh_demo *header, struct mh_demo_record_sample *sample,
	struct mh_error *error_out);
enum mh_error_code	mh_demo_capture_crossings(const struct mh_field *field,
	struct mh_demo_player players[MH_DEMO_PLAYERS],
	struct mh_error *error_out);

/*
 * Advance a local field and capture, before wheels/recovery and progress.
 * Publish field, RNG, slice count and captured bytes together only after all
 * frame stages succeed. The storage adapter consumes these owned bytes after
 * publication; no writes or retained callbacks occur here.
 */
enum mh_error_code	mh_demo_capture_advance(struct mh_field *field,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, const struct mh_demo *header,
	struct mh_demo_capture_frame *capture, unsigned int *steps,
	unsigned long *random, struct mh_error *error_out);

#endif
