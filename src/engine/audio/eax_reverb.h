#ifndef MH_EAX_REVERB_H
#define MH_EAX_REVERB_H

#include "engine/audio/eax_listener.h"
#include "engine/audio/reverb.h"

/*
 * Native audible approximation of EAX 1's environment requests. Creation uses
 * the game's volume=0, decay=1 second, damping=.5 writes. Updates consume the
 * already-scaled listener request (do not scale again).
 * Own/serialize/process/clear/destroy the result through reverb.h. This is only
 * the environment bus, not the EAX queue or a complete race profile.
 */
enum mh_error_code	mh_eax_reverb_create(unsigned long rate,
	struct mh_reverb **output, struct mh_error *error_out);
enum mh_error_code	mh_eax_reverb_update(struct mh_reverb *reverb,
	const struct mh_eax_listener_update *update,
	struct mh_error *error_out);

#endif
