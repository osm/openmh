#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/simulation/route_curve.h"
#include "server/session/grid.h"
#include "server/session/host_internal.h"
#include "server/session/setup.h"
#include "server/session/standings.h"

#define GRID_ROW_DISTANCE 10.0f
#define GRID_START_DISTANCE 10.0f
#define GRID_SIDE_DISTANCE 2.5f
#define GRID_STAGGER_DISTANCE 2.0f

enum {
	CLOCK_TEXT_BYTES = 64,
	ELIMINATION_TEXT_BYTES = 128,
	COUNTDOWN_TEXT_THRESHOLD = 5
};

enum mh_error_code
mh_server_host_attach_courses(struct mh_server_host *host,
	const struct mh_server_courses *courses, struct mh_error *error_out)
{
	if (host == NULL || courses == NULL || courses->count == 0 ||
		courses->count != host->config->track_count ||
		courses->selected >= courses->count || host->race.active)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated host courses");

	host->courses = courses;
	host->course = courses->selected;
	strcpy(host->lobby.roster.track,
		host->config->tracks[host->course].name);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Fixed server grid, before any remote pose has arrived. */
static enum mh_error_code
place_on_grid(const struct mh_route *route, unsigned int position,
	double output[3], struct mh_error *error_out)
{
	float sample[9], forward[3], side[3], distance, lateral, origin[3];
	double squared;
	long double length, inverse;
	unsigned int i, slot;
	enum mh_error_code status;

	status = mh_route_curve_sample(route, 0, sample, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_route_curve_direction(route, 0, forward, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	slot = position - 1;
	distance = -(float)(slot / 2) * GRID_ROW_DISTANCE - GRID_START_DISTANCE;

	if (slot & 1)
		distance -= GRID_STAGGER_DISTANCE;

	lateral = slot & 1 ? GRID_SIDE_DISTANCE : -GRID_SIDE_DISTANCE;
	side[0] = forward[2];
	side[1] = forward[1];
	side[2] = -forward[0];
	squared = (double)(((long double)side[1] * side[1] +
				(long double)side[0] * side[0]) +
		(long double)side[2] * side[2]);

	if (!(squared > 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid server grid direction");

	length = sqrt(squared);
	length = (length + squared / length) * .5L;
	inverse = 1 / length;

	for (i = 0; i < 3; i++) {
		side[i] = (float)(side[i] * inverse);
		origin[i] =
			(float)((long double)forward[i] * distance + sample[i]);
		output[i] = (float)((long double)side[i] * lateral + origin[i]);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_setup(struct mh_server_host *host, const struct mh_server_setup *setup,
	struct mh_error *error_out)
{
	const char *text;
	unsigned int i, flags;
	enum mh_error_code status;

	/* Notice, optional setup, then drain for each peer. */
	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		flags = setup->message.roster.players[i].flags;

		if (!(flags & MH_NET_FLAG_LOBBY))
			continue;

		if (flags & MH_NET_FLAG_READY)
			text = "Entering race.";
		else if (flags & MH_NET_FLAG_PASS)
			text = "Race started...";
		else
			text = "You failed to go ready, missed race start...";
		status = mh_server_host_notice(
			host, 1U << i, MH_NET_CHAT, text, error_out);

		if (status == MH_ERROR_NONE && (setup->racers & (1U << i)))
			status = mh_server_host_send(host, host->lobby.peers[i],
				&setup->message, 1, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_transport_flush(host->transport, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_racers(struct mh_server_host *host, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if ((host->lobby.roster.players[i].flags &
			(MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY)) !=
			(MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY))
			continue;

		status = mh_server_progress_start(
			&host->courses->routes[host->course],
			host->lobby.roster.laps, &host->race.progress[i],
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		host->race.progress[i].result.position =
			host->lobby.reported_a[i];
		host->race.results[i] = host->race.progress[i].result;
		mh_server_vehicle_reset(&host->race.vehicles[i]);
		host->race.vehicles[i].view_player = i;
		status = place_on_grid(&host->courses->routes[host->course],
			host->lobby.reported_a[i],
			host->race.vehicles[i].position, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
begin_race(struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_server_setup setup;
	unsigned long best_laps[MH_NET_WIRE_PLAYERS];
	unsigned int i;
	enum mh_error_code status;

	for (i = 0; i < host->courses->count; i++)
		if (strcmp(host->lobby.roster.track,
			host->config->tracks[i].name) == 0)
			break;

	if (i == host->courses->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"dedicated selected track has no loaded course");

	host->course = i;
	mh_server_event_log(host->event_logger, "Entering race.", NULL);

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		best_laps[i] = host->race.results[i].best_lap_ms;

	status = mh_server_grid_assign(&host->lobby, host->lobby.grid,
		best_laps, &host->random_state, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&host->race, 0, sizeof(host->race));

	status = prepare_racers(host, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_server_setup_begin(&host->lobby, &setup, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	host->race.active = 1;
	host->race.origin_ms = mh_transport_now(host->transport);
	status = send_setup(host, &setup, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_server_host_advertise(host, error_out);
}

static enum mh_error_code
finish_race(
	struct mh_server_host *host, int aborted, struct mh_error *error_out)
{
	struct mh_net_message message;
	char record[MH_SERVER_RACE_RECORD_BYTES];
	int log_race;
	enum mh_error_code status;

	log_race = !aborted && host->race_logger != NULL &&
		host->race_logger->write != NULL;

	if (log_race) {
		status = mh_server_race_record(&host->lobby.roster,
			host->race.results, record, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_server_results_apply(&host->lobby, host->race.results,
		aborted, &host->last_race, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (log_race)
		host->race_logger->write(host->race_logger->context, record);

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_RACE;
	status = mh_server_host_multicast(host,
		mh_server_host_members(host, MH_NET_FLAG_RACING), &message, 1,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (host->config->step_track) {
		host->course = (host->course + 1) % host->courses->count;
		strcpy(host->lobby.roster.track,
			host->config->tracks[host->course].name);
	}

	mh_server_lobby_resume(&host->lobby);
	memset(&host->ready, 0, sizeof(host->ready));
	host->race.active = 0;
	mh_server_event_log(host->event_logger, "Back to lobby.", NULL);

	return mh_server_host_advertise(host, error_out);
}

enum mh_error_code
mh_server_host_abort_race(
	struct mh_server_host *host, struct mh_error *error_out)
{
	return finish_race(host, 1, error_out);
}

enum mh_error_code
mh_server_host_countdown_notice(
	struct mh_server_host *host, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (!(host->lobby.roster.players[i].flags & MH_NET_FLAG_LOBBY))
			continue;

		status = mh_server_host_notice(host, 1U << i, MH_NET_CHAT,
			host->lobby.roster.players[i].flags & MH_NET_FLAG_READY
				? "Race will start in 20 seconds..."
				: "Race will start in 20 seconds,    GO READY!!",
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_readiness(struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_server_ready_events events;
	char text[CLOCK_TEXT_BYTES];
	enum mh_error_code status;

	status = mh_server_ready_tick(&host->ready, &host->lobby, host->config,
		mh_transport_now(host->transport), &events, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (events.countdown_started) {
		status = mh_server_host_countdown_notice(host, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_server_event_log(host->event_logger,
			"Race will start in 20 seconds...", NULL);
	}

	if (events.countdown_second != 0) {
		if (events.countdown_second >= COUNTDOWN_TEXT_THRESHOLD)
			sprintf(text, "%u seconds to race start!",
				events.countdown_second);
		else
			sprintf(text, "%u", events.countdown_second);
		mh_server_event_log(host->event_logger, text, NULL);
		status = mh_server_host_notice(host,
			mh_server_host_members(host, MH_NET_FLAG_LOBBY),
			MH_NET_CHAT, text, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (host->ready.start_requested) {
		if (mh_server_lobby_synchronized(&host->lobby))
			return begin_race(host, error_out);

		mh_server_event_log(host->event_logger,
			"Waiting for players to return from race...", NULL);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
clock_notice(struct mh_server_host *host, enum mh_server_clock_notice notice,
	unsigned int recipients, unsigned long remaining,
	struct mh_error *error_out)
{
	char text[CLOCK_TEXT_BYTES];

	if (notice == MH_SERVER_CLOCK_NOTICE_NONE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	text[0] = 0;

	if (notice == MH_SERVER_CLOCK_TIME_LEFT)
		sprintf(text, "Time left: %lu", remaining);
	else if (notice == MH_SERVER_CLOCK_DID_NOT_FINISH)
		strcpy(text, "Did not finish!");
	else if (notice == MH_SERVER_CLOCK_RACE_ENDING)
		strcpy(text, "Race Time Up!");

	return mh_server_host_notice(
		host, recipients, MH_NET_NOTICE, text, error_out);
}

static enum mh_error_code
advance_clock(struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_server_clock_sample sample;
	struct mh_server_clock_tick tick;
	unsigned int i;
	enum mh_error_code status;

	memset(&sample, 0, sizeof(sample));
	sample.mode = (enum mh_server_mode)host->lobby.roster.mode;
	sample.racing = mh_server_host_members(host, MH_NET_FLAG_RACING);

	if (sample.racing == 0)
		return finish_race(host, 1, error_out);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
		if (host->race.results[i].finished)
			sample.finished |= 1U << i;

	sample.eliminated = host->race.elimination.eliminated;
	sample.countdown_ms = host->race.start.started_ms;
	sample.now_ms =
		mh_transport_now(host->transport) - host->race.origin_ms;
	status = mh_server_clock_step(
		&host->race.clock, host->config, &sample, &tick, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	host->race.elapsed_ms = tick.elapsed_ms;
	status = clock_notice(host, tick.finish_notice, tick.finish_recipients,
		tick.remaining_seconds, error_out);

	if (status == MH_ERROR_NONE)
		status = clock_notice(host, tick.limit_notice,
			tick.limit_recipients, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (tick.end != MH_SERVER_CLOCK_RUNNING)
		return finish_race(host, 0, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_host_race_before(
	struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_net_message message;
	unsigned int due;
	unsigned long started;
	enum mh_error_code status;

	if (!host->race.active) {
		if (host->courses == NULL)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		return check_readiness(host, error_out);
	}

	status = advance_clock(host, error_out);

	if (status != MH_ERROR_NONE || !host->race.active)
		return status;

	started = host->race.start.started_ms;
	status = mh_server_setup_step(&host->lobby, &host->race.start,
		mh_transport_now(host->transport) - host->race.origin_ms, &due,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_START;
	status = mh_server_host_multicast(host, due, &message, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (started == 0 && host->race.start.started_ms != 0) {
		mh_server_event_log(host->event_logger,
			"All players in race. Race started!", NULL);
		status = mh_server_host_notice(host,
			mh_server_host_members(host, MH_NET_FLAG_PRESENT),
			MH_NET_CHAT, "All players in race. Race started!",
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_server_host_replicate(host, error_out);
}

static enum mh_error_code
elimination_notices(struct mh_server_host *host,
	const struct mh_net_elimination_state *state,
	struct mh_error *error_out)
{
	const struct mh_net_elimination_notice *notice;
	char text[ELIMINATION_TEXT_BYTES];
	unsigned int i;
	enum mh_error_code status;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		notice = &state->notices[i];

		if (notice->kind == MH_NET_ELIM_NONE)
			continue;

		text[0] = 0;

		if (notice->kind == MH_NET_ELIM_TIME)
			sprintf(text, "Time left: %lu", notice->remaining);
		else if (notice->kind == MH_NET_ELIM_OUT)
			sprintf(text, "You have been eliminated by %s",
				host->lobby.roster.players[notice->leader]
					.name);
		else if (notice->kind == MH_NET_ELIM_WIN)
			strcpy(text, "You are the winner!");

		status = mh_server_host_notice(
			host, 1U << i, MH_NET_NOTICE, text, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (notice->kind == MH_NET_ELIM_OUT) {
			sprintf(text, "%s has been eliminated by %s!",
				host->lobby.roster.players[i].name,
				host->lobby.roster.players[notice->leader]
					.name);
			mh_server_event_log(host->event_logger, text, NULL);
			status = mh_server_host_notice(host,
				mh_server_host_members(
					host, MH_NET_FLAG_RACING),
				MH_NET_CHAT, text, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	if (state->winner < MH_NET_WIRE_PLAYERS) {
		sprintf(text, "%s is the winner!",
			host->lobby.roster.players[state->winner].name);
		mh_server_event_log(host->event_logger, text, NULL);
		return mh_server_host_notice(host,
			mh_server_host_members(host, MH_NET_FLAG_RACING),
			MH_NET_CHAT, text, error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
advance_elimination(struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_net_elimination_sample samples[MH_NET_WIRE_PLAYERS];
	struct mh_net_elimination_state state;
	const struct mh_server_progress *progress;
	unsigned int i;
	enum mh_error_code status;

	memset(samples, 0, sizeof(samples));

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		progress = &host->race.progress[i];
		samples[i].lap = (unsigned int)progress->result.lap;
		samples[i].lap_limit = progress->lap_limit;
		samples[i].position = host->race.results[i].position;
		samples[i].split = progress->split_notice;
		samples[i].elapsed_ms = progress->result.elapsed_ms;
	}

	status = mh_net_elimination_step_dedicated(&host->race.elimination,
		samples, host->lobby.roster.player_count,
		mh_server_host_members(host, MH_NET_FLAG_RACING),
		mh_transport_now(host->transport) - host->race.origin_ms,
		&state, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = elimination_notices(host, &state, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (state.ended)
		return finish_race(host, 0, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sample_racers(struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_server_progress *progress;
	unsigned int i;
	enum mh_error_code status;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (!(host->lobby.roster.players[i].flags & MH_NET_FLAG_RACING))
			continue;

		status = mh_server_vehicle_sample(
			&host->race.vehicles[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		progress = &host->race.progress[i];

		if (!(host->race.elimination.eliminated & (1U << i))) {
			status = mh_server_progress_step(progress,
				host->race.vehicles[i].position,
				host->race.elapsed_ms, progress, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		host->race.results[i] = progress->result;
		host->race.results[i].top_speed_kmh =
			host->race.vehicles[i].top_speed_kmh;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_host_race_after(
	struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_server_standings standings;
	unsigned int i;
	enum mh_error_code status;

	if (!host->race.active)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* Sample, project, eliminate using previous ranks, rank. */
	status = sample_racers(host, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (host->lobby.roster.mode == MH_NET_MODE_ELIMINATION) {
		status = advance_elimination(host, error_out);

		if (status != MH_ERROR_NONE || !host->race.active)
			return status;
	}

	status = mh_server_standings_rank(
		&host->lobby, host->race.results, &standings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
		host->race.progress[i].result.position =
			host->race.results[i].position;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
