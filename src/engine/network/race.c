#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/race_limits.h"
#include "engine/network/race_internal.h"
#include "engine/network/snapshot_schedule.h"
#include "engine/simulation/field_internal.h"

#define FRAME_ELAPSED_LIMIT 1e9

static int
entry_unavailable(const struct mh_net_race *race,
	const struct mh_net_session *session, unsigned int entry, double now_ms)
{
	unsigned int player;
	const struct mh_net_replication *replication;

	player = race->layout.player[entry];
	replication = &session->replication;

	if (!session->host)
		return replication->status_revision != 0 &&
			(replication->status[player].flags &
				MH_NET_FLAG_UNAVAILABLE);

	if (session->roster.mode == MH_NET_MODE_ELIMINATION &&
		(race->elimination.eliminated & (1U << player)))
		return 1;

	if (entry == 0)
		return 0;

	return mh_net_snapshot_stale(session->config.wire,
		replication->history[player].received_ms, now_ms);
}

static void
update_availability(struct mh_net_race *race,
	const struct mh_net_session *session, double now_ms)
{
	unsigned int entry;

	race->field.unavailable_mask = 0;

	for (entry = 0; entry < race->field.count; entry++) {
		if (!(race->field.active_mask & (1U << entry)))
			continue;

		if (entry_unavailable(race, session, entry, now_ms))
			race->field.unavailable_mask |= 1U << entry;
	}
}

enum mh_error_code
mh_net_race_layout(const struct mh_net_session *session,
	struct mh_net_race_layout *output, struct mh_error *error_out)
{
	struct mh_net_race_layout layout;
	unsigned int i, rank, rank_mask;

	if (session == NULL || output == NULL ||
		session->phase != MH_NET_LOADING ||
		session->roster.player_count < 1 ||
		session->roster.player_count >
			mh_net_session_capacity(session) ||
		session->player >= session->roster.player_count ||
		!(session->roster.players[session->player].flags &
			MH_NET_FLAG_RACING) ||
		session->roster.laps < MH_RACE_MIN_LAPS ||
		session->roster.laps > MH_RACE_MAX_LAPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loaded network race roster");

	if (session->roster.mode > MH_NET_MODE_ELIMINATION)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported network race mode");

	memset(&layout, 0, sizeof(layout));

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		layout.entry[i] = MH_NET_WIRE_PLAYERS;

	layout.player[0] = session->player;
	layout.entry[session->player] = 0;
	layout.count = 1;

	for (i = 0; i < session->roster.player_count; i++)
		if (i != session->player &&
			(session->roster.players[i].flags &
				MH_NET_FLAG_RACING)) {
			layout.player[layout.count] = i;
			layout.entry[i] = layout.count++;
		}

	rank_mask = 0;

	for (i = 0; i < layout.count; i++) {
		rank = session->roster.players[layout.player[i]].value_a;

		/*
		 * Departures while loading can leave valid holes in the
		 * original grid.
		 */
		if (rank < 1 || rank > mh_net_session_capacity(session) ||
			(rank_mask & (1U << rank)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network grid rank");

		rank_mask |= 1U << rank;

		if (session->roster.players[layout.player[i]].gear_mode > 1)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported network transmission");
	}

	*output = layout;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
publish_field(struct mh_net_race *race, struct mh_net_session *session,
	unsigned int viewed_entry, struct mh_error *error_out)
{
	struct mh_net_vehicle vehicle;
	unsigned int entry;
	enum mh_error_code status;

	status = mh_net_drive_capture(
		&race->field.cars[0], NULL, &vehicle, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_session_publish(session, &vehicle,
			race->layout.player[viewed_entry], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (session->host)
		for (entry = 0; entry < race->layout.count; entry++)
			if (race->field.active_mask & (1U << entry)) {
				/*
				 * Publish classified position and lap from race
				 * progress.
				 */
				status = mh_net_session_status(session,
					race->layout.player[entry],
					race->field.standings.cars[entry]
						.position,
					race->field.cars[entry].race.lap,
					error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_race_start(struct mh_net_session *session,
	const struct mh_car *const *cars, const struct mh_collision *hulls,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_route *route, const struct mh_track *track,
	struct mh_net_race *output, struct mh_error *error_out)
{
	struct mh_net_session next_session;
	struct mh_net_race race;
	struct mh_drive drives[MH_NET_WIRE_PLAYERS];
	double positions[MH_NET_WIRE_PLAYERS][3], velocity[3];
	const struct mh_net_profile *profile;
	enum mh_error_code status;
	unsigned int i, player;

	if (output == NULL || cars == NULL || hulls == NULL || track == NULL ||
		session == NULL || track->laps != session->roster.laps ||
		session->replication.configured)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network race assets");

	memset(&race, 0, sizeof(race));
	memset(positions, 0, sizeof(positions));
	race.winner = race.announced_winner = MH_NET_PLAYERS;

	for (i = 0; i < session->roster.player_count && i < MH_NET_PLAYERS; i++)
		memcpy(race.player_names[i], session->roster.players[i].name,
			sizeof(race.player_names[i]));

	race.finish_state.remaining = -1;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		race.finish_warning[i] = MH_NET_RACE_WARNING_NONE;

	status = mh_net_race_layout(session, &race.layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next_session = *session;

	for (i = 0; i < race.layout.count; i++) {
		player = race.layout.player[i];
		profile = &session->roster.players[player];

		/* Mode 4 dispatch subtracts one from wire rank. */
		status = mh_drive_start(cars[i], col, materials, route, track,
			profile->value_a - 1, profile->gear_mode == 0, 1,
			&drives[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * A peer can depart before its first history sample. Record its
		 * initialized grid pose, not a zeroed cache at the world
		 * origin.
		 */
		status = mh_net_drive_capture(&drives[i], NULL,
			&race.samples[i].replica.vehicle, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * Clear the introduction on network entry. Diagnostics retain
		 * their 1ms epoch; live entry binds the process clock. Neither
		 * permits GO/media before a host/client release.
		 */
		drives[i].frame.intro_ms = 0;
		drives[i].remote = i != 0;
		memcpy(drives[i].remote_extension,
			drives[i].vehicle.motion.extension,
			sizeof(drives[i].remote_extension));
		status = mh_motion_display(&drives[i].vehicle.motion,
			positions[player], velocity, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_field_start_network(
		drives, race.layout.count, &race.field, error_out);

	if (status == MH_ERROR_NONE && next_session.config.simulation_rate)
		status = mh_field_simulation_rate(&race.field,
			next_session.config.simulation_rate, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(
			&race.field, hulls, race.layout.count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_session_configure_replication(&next_session,
			track->view_distance, (const double (*)[3])positions,
			error_out);

	if (status == MH_ERROR_NONE)
		status = publish_field(&race, &next_session, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_session_loaded(
			&next_session, next_session.now_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	race.now_ms = next_session.now_ms;
	*output = race;
	*session = next_session;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
update_remote_entries(struct mh_net_race *race, struct mh_net_session *session,
	double now_ms, struct mh_error *error_out)
{
	struct mh_net_playback playback;
	double retained[3], velocity[3];
	unsigned int entry, player, active_mask;
	enum mh_error_code status;

	race->rejected_mask = 0;
	active_mask = race->field.active_mask;

	for (entry = 0; entry < race->layout.count; entry++) {
		player = race->layout.player[entry];

		if (player >= session->roster.player_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network field slot mapping");

		if (!(session->roster.players[player].flags &
			MH_NET_FLAG_RACING)) {
			active_mask &= ~(1U << entry);
			continue;
		}

		if (entry == 0)
			continue;

		status = mh_net_session_sample(
			session, player, now_ms, &playback, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_net_drive_apply(&race->field.cars[entry], &playback,
			&race->samples[entry], error_out);

		if (status != MH_ERROR_NONE) {
			race->rejected_mask |= 1U << entry;
			status = mh_motion_display(
				&race->field.cars[entry].vehicle.motion,
				retained, velocity, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			memcpy(session->replication.position[player], retained,
				sizeof(retained));
		}
	}

	if (!(active_mask & 1))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"local player left network race");

	return mh_field_set_active(&race->field, active_mask, error_out);
}

enum mh_error_code
mh_net_race_frame_physics(struct mh_net_race *race,
	struct mh_net_session *session, const struct mh_engine_input *controls,
	long double elapsed, double now_ms, unsigned int viewed_entry,
	unsigned int *steps, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	if (race == NULL || session == NULL || controls == NULL ||
		steps == NULL || !session->replication.configured ||
		(session->phase != MH_NET_WAITING &&
			session->phase != MH_NET_RACING) ||
		race->layout.count < 1 ||
		race->layout.count > mh_net_session_capacity(session) ||
		race->field.count != race->layout.count ||
		race->layout.player[0] != session->player ||
		viewed_entry >= race->layout.count ||
		!(elapsed >= 0 && elapsed <= FRAME_ELAPSED_LIMIT) ||
		!(now_ms >= race->now_ms && now_ms >= session->now_ms &&
			now_ms <= (double)MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network race frame");

	/*
	 * Sample the game clock before host finish checks.
	 * Transport/interpolation retain their separate performance-counter
	 * clock.
	 */
	status = mh_field_frame_begin(&race->field, elapsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	session->now_ms = now_ms;
	race->now_ms = now_ms;
	*steps = 0;
	status = mh_net_race_check_finish(race, session, error_out);

	if (status != MH_ERROR_NONE || race->ended)
		return status;

	status = mh_net_session_start_frame(
		session, mh_drive_clock_ms(&race->field.cars[0]), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!race->started && session->phase == MH_NET_RACING &&
		session->start.started_ms >
			mh_drive_clock_ms(&race->field.cars[0]))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"network start game clock is in the future");

	status = update_remote_entries(race, session, now_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	update_availability(race, session, now_ms);

	if (!(race->field.active_mask & (1U << viewed_entry)))
		viewed_entry = 0;

	status = mh_field_select_view(&race->field, viewed_entry, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!race->started && session->phase == MH_NET_RACING)
		race->start_tick = race->field.cars[0].ticks;

	status = mh_field_frame_simulate(
		&race->field, controls, steps, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!race->started && session->phase == MH_NET_RACING) {
		/*
		 * Store the current GAME clock, not transport time. GO and
		 * scenery animation were already sampled with the old intro.
		 * Publish this after physics so those pre-network consumers
		 * retain that sample; late media/presentation sees the released
		 * intro. No clipping away the physics interval on the release
		 * frame.
		 */
		for (i = 0; i < race->field.count; i++) {
			race->field.cars[i].frame.intro_ms =
				session->start.started_ms;
			race->field.cars[i].start_ticks = 0;
		}

		race->started = 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_race_frame_finish(struct mh_net_race *race,
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_standings prior_standings;
	enum mh_error_code status;

	if (race->ended)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	prior_standings = race->field.standings;
	status = mh_field_frame_update(&race->field,
		race->field.cars[0].frame.time.elapsed, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_frame_progress(&race->field, error_out);

	/*
	 * Update each eligible progress object, then call the elimination
	 * controller once. Classify only afterwards.
	 */
	if (status == MH_ERROR_NONE)
		status = mh_net_race_update_elimination(
			race, session, &prior_standings, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_race_announce_elimination(
			race, session, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (session->host && session->roster.mode == MH_NET_MODE_ELIMINATION &&
		race->elimination_state.ended)
		return mh_net_race_complete(race, session, error_out);

	status = mh_field_frame_results(&race->field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return publish_field(
		race, session, mh_field_viewed(&race->field), error_out);
}

enum mh_error_code
mh_net_race_frame(struct mh_net_race *race, struct mh_net_session *session,
	const struct mh_engine_input *controls, long double elapsed,
	double now_ms, unsigned int viewed_entry, unsigned int *steps,
	struct mh_error *error_out)
{
	struct mh_net_race next;
	struct mh_net_session next_session;
	unsigned int step_count;
	enum mh_error_code status;

	if (race == NULL || session == NULL || steps == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing network frame output");

	next = *race;
	next_session = *session;
	status = mh_net_race_frame_physics(&next, &next_session, controls,
		elapsed, now_ms, viewed_entry, &step_count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_race_frame_finish(
			&next, &next_session, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*race = next;
	*session = next_session;
	*steps = step_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
