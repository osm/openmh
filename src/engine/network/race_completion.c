#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/network/race_internal.h"

#define NOTICE_COLOR 0xffffffUL
#define CHAT_COLOR 0xe06820UL

enum mh_error_code
mh_net_race_update_elimination(struct mh_net_race *race,
	const struct mh_net_session *session,
	const struct mh_standings *prior_standings, struct mh_error *error_out)
{
	struct mh_net_elimination_sample samples[MH_NET_PLAYERS];
	const struct mh_race *progress;
	unsigned int entry, player, active_player_mask;
	enum mh_error_code status;

	if (!session->host || session->phase != MH_NET_RACING ||
		session->roster.mode != MH_NET_MODE_ELIMINATION)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(samples, 0, sizeof(samples));
	active_player_mask = 0;

	for (entry = 0; entry < race->field.count; entry++)
		if (race->field.active_mask & (1U << entry)) {
			player = race->layout.player[entry];
			active_player_mask |= 1U << player;
			progress = &race->field.cars[entry].race;
			samples[player].lap = progress->lap;
			samples[player].lap_limit = progress->lap_limit;
			samples[player].position =
				prior_standings->cars[entry].position;
			samples[player].split = progress->split_event;
			samples[player].elapsed_ms = progress->now_ms;
		}

	status = mh_net_elimination_step(&race->elimination, samples,
		session->roster.player_count, active_player_mask,
		mh_drive_clock_ms(&race->field.cars[0]),
		&race->elimination_state, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (entry = 0; entry < race->field.count; entry++) {
		player = race->layout.player[entry];
		race->field.cars[entry].race_frozen =
			(race->elimination.eliminated & (1U << player)) != 0;

		if (race->elimination_state.notices[player].kind !=
			MH_NET_ELIM_NONE)
			race->elimination_notices[player] =
				race->elimination_state.notices[player];
	}

	if (race->elimination_state.winner != MH_NET_PLAYERS)
		race->winner = race->elimination_state.winner;

	/*
	 * Mark elimination only. Publish unavailable on the NEXT service pass,
	 * after that frame's physics list is already built.
	 */

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
format_elimination_notice(const struct mh_net_race *race,
	const struct mh_net_elimination_notice *notice, char *message)
{
	message[0] = 0;

	if (notice->kind == MH_NET_ELIM_TIME)
		sprintf(message, "Time left: %lu", notice->remaining);

	if (notice->kind == MH_NET_ELIM_OUT)
		sprintf(message, "You have been eliminated by %.15s",
			race->player_names[notice->leader]);

	if (notice->kind == MH_NET_ELIM_WIN)
		strcpy(message, "You are the winner!");
}

enum mh_error_code
mh_net_race_announce_elimination(struct mh_net_race *race,
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_announcement events[MH_NET_ANNOUNCEMENTS];
	const struct mh_net_elimination_notice *notice;
	char message[MH_NET_ANNOUNCEMENT_BYTES];
	unsigned int entry, player, active_player_mask, count, leader;
	enum mh_error_code status;

	if (!session->host || session->phase != MH_NET_RACING ||
		session->roster.mode != MH_NET_MODE_ELIMINATION)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(events, 0, sizeof(events));
	active_player_mask = count = 0;

	for (entry = 0; entry < race->field.count; entry++)
		if (race->field.active_mask & (1U << entry))
			active_player_mask |= 1U << race->layout.player[entry];

	for (player = 0; player < MH_NET_PLAYERS; player++) {
		if (!(active_player_mask & (1U << player)))
			continue;

		notice = &race->elimination_notices[player];

		if (notice->kind == MH_NET_ELIM_NONE)
			continue;

		format_elimination_notice(race, notice, message);

		if (strcmp(message, race->elimination_text[player]) != 0) {
			events[count].recipients = 1U << player;
			events[count].color = NOTICE_COLOR;
			strcpy(events[count++].text, message);
			strcpy(race->elimination_text[player], message);
		}

		if ((race->elimination.eliminated & (1U << player)) &&
			!(race->announced_eliminated & (1U << player))) {
			leader = notice->leader;
			events[count].recipients = active_player_mask;
			events[count].chat = 1;
			events[count].color = CHAT_COLOR;
			sprintf(events[count++].text,
				"%.15s has been eliminated by %.15s!",
				race->player_names[player],
				race->player_names[leader]);
			race->announced_eliminated |= 1U << player;
		}
	}

	if (race->winner != MH_NET_PLAYERS &&
		race->winner != race->announced_winner) {
		events[count].recipients = active_player_mask;
		events[count].chat = 1;
		events[count].color = CHAT_COLOR;
		sprintf(events[count++].text, "%.15s is the winner!",
			race->player_names[race->winner]);
		race->announced_winner = race->winner;
	}

	status =
		mh_net_session_announcements(session, events, count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	session->replication.eliminated = race->elimination.eliminated;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_race_complete(struct mh_net_race *race, struct mh_net_session *session,
	struct mh_error *error_out)
{
	struct mh_net_session next;
	unsigned int entry, positions[MH_NET_PLAYERS];
	enum mh_error_code status;

	next = *session;
	memset(positions, 0, sizeof(positions));

	for (entry = 0; entry < race->field.count; entry++)
		if (race->field.active_mask & (1U << entry)) {
			positions[race->layout.player[entry]] =
				race->field.standings.cars[entry].position;

			/*
			 * Elimination can exit before the frame's regular
			 * publication.
			 */
			if (next.config.wire == MH_NET_WIRE_31_LOBBY) {
				status = mh_net_session_status(&next,
					race->layout.player[entry],
					race->field.standings.cars[entry]
						.position,
					race->field.cars[entry].race.lap,
					error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}
		}

	status = mh_net_session_finish_race(&next, positions, error_out);

	if (status == MH_ERROR_NONE) {
		*session = next;
		race->ended = 1;
	}

	return status;
}

enum mh_error_code
mh_net_race_return(struct mh_net_race *race, struct mh_net_session *session,
	struct mh_error *error_out)
{
	if (race == NULL || session == NULL || race->field.count == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network race return");

	if (session->phase == MH_NET_LOBBY)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* Test the local physical finish flag, not the viewed car. */
	if (session->config.wire == MH_NET_WIRE_31_LOBBY && session->host &&
		session->phase == MH_NET_RACING &&
		race->field.cars[0].race.finished)
		return mh_net_race_complete(race, session, error_out);

	return mh_net_session_return_lobby(session, error_out);
}

enum mh_error_code
mh_net_race_check_finish(struct mh_net_race *race,
	struct mh_net_session *session, struct mh_error *error_out)
{
	unsigned int entry, player, finished_entries, eliminated_entries;
	int notice;
	char message[64];
	enum mh_error_code status;

	if (!session->host || session->phase != MH_NET_RACING)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	finished_entries = eliminated_entries = 0;

	for (entry = 0; entry < race->field.count; entry++) {
		if (race->field.cars[entry].race.finished)
			finished_entries |= 1U << entry;

		player = race->layout.player[entry];

		if (race->elimination.eliminated & (1U << player))
			eliminated_entries |= 1U << entry;
	}

	status = mh_net_finish_step_mode(&race->finish, session->roster.mode,
		race->field.active_mask, finished_entries, eliminated_entries,
		mh_drive_clock_ms(&race->field.cars[0]), &race->finish_state,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (race->finish_state.ended)
		return mh_net_race_complete(race, session, error_out);

	if (race->finish_state.remaining < 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	notice = race->finish_state.expired ? MH_NET_RACE_WARNING_DNF
					: race->finish_state.remaining;

	if (notice == MH_NET_RACE_WARNING_DNF)
		strcpy(message, "Did not finish!");
	else
		sprintf(message, "Time left: %d", notice);

	for (entry = 0; entry < race->field.count; entry++) {
		if (!(race->field.active_mask & (1U << entry)) ||
			(finished_entries & (1U << entry)) ||
			race->finish_warning[entry] == notice)
			continue;

		status = mh_net_session_notice(session,
			race->layout.player[entry], message, NOTICE_COLOR,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		race->finish_warning[entry] = notice;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
