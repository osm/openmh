#ifndef MH_NET_RACE_H
#define MH_NET_RACE_H

#include "engine/core/error.h"
#include "engine/network/drive.h"
#include "engine/network/elimination.h"
#include "engine/network/finish.h"
#include "engine/network/session.h"
#include "engine/simulation/field.h"

struct mh_net_race_layout {
	unsigned int	count;

	/* Field entry -> wire slot; local first. */
	unsigned int	player[MH_NET_WIRE_PLAYERS];

	/* Wire slot -> field entry; MH_NET_WIRE_PLAYERS means absent. */
	unsigned int	entry[MH_NET_WIRE_PLAYERS];
};

struct mh_net_race {
	struct mh_field				field;
	struct mh_net_race_layout		layout;
	struct mh_net_drive_sample		samples[MH_NET_WIRE_PLAYERS];

	/* Field entries whose latest pose exceeded motion bounds. */
	unsigned int				rejected_mask;
	struct mh_net_finish			finish;
	struct mh_net_finish_state		finish_state;
	struct mh_net_elimination		elimination;
	struct mh_net_elimination_state		elimination_state;

	/*
	 * Listen-host completion state remains limited to eight players.
	 * Latest desired notice and last delivered text, indexed by wire slot.
	 */
	struct mh_net_elimination_notice	elimination_notices[MH_NET_PLAYERS];
	char					elimination_text[MH_NET_PLAYERS][MH_NET_ANNOUNCEMENT_BYTES];

	/* Preserve eliminator identity after departure. */
	char					player_names[MH_NET_PLAYERS][MH_NET_NAME_BYTES];
	unsigned int				announced_eliminated;

	/* Winner wire slots; MH_NET_PLAYERS means none. */
	unsigned int				announced_winner, winner;

	/*
	 * Last warning sent to each field entry; -1 means none, -2 means DNF.
	 */
	int					finish_warning[MH_NET_PLAYERS];

	int					started, ended;

	/* Local lifetime tick at release of the loading barrier. */
	unsigned long				start_tick;

	/* Transport time; independent of the field's sampled game clock. */
	double					now_ms;
};

/*
 * Validate the frozen roster and construct stable local-first entry bindings.
 * Mode 4 places cars at wire rank-1. Listen games retain eight slots;
 * patched clients can map the dedicated server's twelve remote drivers.
 * Original gear_mode 1 is automatic, 0 manual. Supports normal/elimination.
 */
enum mh_error_code	mh_net_race_layout(const struct mh_net_session *session,
	struct mh_net_race_layout *output, struct mh_error *error_out);

/*
 * Borrow resolved cars/hulls in layout entry order and one shared track.
 * Track laps must already match the roster. Starts every car, configures
 * replication, publishes the local grid pose and declares this peer loaded.
 * Both session and race outputs are atomic. Borrowed assets must outlive race.
 */
enum mh_error_code	mh_net_race_start(struct mh_net_session *session,
	const struct mh_car *const *cars, const struct mh_collision *hulls,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_route *route, const struct mh_track *track,
	struct mh_net_race *output, struct mh_error *error_out);

/*
 * Call after servicing transport; samples each remote once, then advances the
 * field with the frame clock and at most ten .04-second slices. WAITING keeps
 * the introduction epoch zero while local suspension settles; RACING releases
 * it at the sampled game clock (GO strictly after 12 seconds). No network-wide
 * pause or release-frame physics clipping. Transport time is independent of
 * game time; elapsed advances the latter without a .25 cap. Excessive remote
 * extrapolation retains its prior pose and sets rejected_mask.
 *
 * Field entries remain stable when a player leaves; active_mask excludes it
 * from ticks/contact/classification.
 *
 * The host sends original countdown notices and final round scores, then
 * returns all peers to the lobby on the deadline before physics. Mode 1
 * consumes frame-end checkpoints, freezes eliminated host progress and supplies
 * targeted notices. Unavailable cars keep membership but leave physics/contact.
 * ended records scored completion on the host. Does not service transport or
 * session_tick. Errors preserve all outputs.
 */
enum mh_error_code	mh_net_race_frame(struct mh_net_race *race,
	struct mh_net_session *session, const struct mh_engine_input *controls,
	long double elapsed, double now_ms, unsigned int viewed_entry,
	unsigned int *steps, struct mh_error *error_out);

/*
 * Explicit local return. Patched hosts award the current classification only
 * after their local car finishes; clients never award themselves. Repeated
 * returns are harmless. Failure preserves race/session state.
 */
enum mh_error_code	mh_net_race_return(struct mh_net_race *race,
	struct mh_net_session *session, struct mh_error *error_out);

#endif
