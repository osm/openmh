#ifndef MH_DEMO_NETWORK_CAPTURE_H
#define MH_DEMO_NETWORK_CAPTURE_H

#include "engine/core/error.h"
#include "engine/demo/capture.h"
#include "engine/demo/record.h"
#include "engine/network/race.h"

/*
 * New takes use MDE 3.0 and preserve negotiated horn names.
 * Freeze a fresh network race into original wire-slot metadata. The local
 * field entry is zero, but its recording slot can be any of the thirteen wire
 * slots. Dedicated races leave server slot zero inactive and retain driver slot
 * twelve. The session is used only here, never during subsequent lobby
 * departures.
 */
enum mh_error_code	mh_network_capture_header(
	const struct mh_net_race *race, const struct mh_net_session *session,
	struct mh_demo *header, struct mh_error *error_out);

/*
 * Preserve every initial body, including departed/unavailable cars. Remote
 * channels come from the last accepted application cache, local channels
 * from live simulation. Masks, bodies and crossings use original wire slots.
 * All outputs are unchanged on failure; no allocation or OS dependency.
 */
enum mh_error_code	mh_network_capture_sample(
	const struct mh_net_race *race, const struct mh_demo *header,
	struct mh_demo_record_sample *sample, struct mh_error *error_out);
enum mh_error_code	mh_network_capture_crossings(
	const struct mh_net_race *race,
	struct mh_demo_player players[MH_DEMO_MAX_PLAYERS],
	struct mh_error *error_out);

/*
 * Atomic network frame and owned pre-recovery capture. Uses the sampled
 * game clock after the released intro's strict six-second gate; transport
 * time remains independent. Ended frames have no capture. No file I/O.
 */
enum mh_error_code	mh_network_capture_advance(struct mh_net_race *race,
	struct mh_net_session *session, const struct mh_engine_input *controls,
	long double elapsed, double now_ms, unsigned int viewed_entry,
	const struct mh_demo *header, struct mh_demo_capture_frame *capture,
	unsigned int *steps, struct mh_error *error_out);

#endif
