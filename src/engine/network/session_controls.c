#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/session_internal.h"

static void
load_command_player(const struct mh_net_profile *profile,
	struct mh_lobby_command_player *player)
{
	unsigned int i;

	memset(player, 0, sizeof(*player));
	memcpy(player->name, profile->name, sizeof(profile->name));
	memcpy(player->team, profile->team, sizeof(profile->team));
	memcpy(player->car, profile->car, sizeof(profile->car));
	memcpy(player->horn, profile->horn, sizeof(profile->horn));

	for (i = 0; i < 4; i++)
		player->color[i] = profile->values[i];

	player->rate = profile->update_rate;
	player->gear = profile->gear_mode;
	player->recording = (profile->flags & MH_NET_FLAG_RECORDING) != 0;
	player->races = profile->values[MH_NET_PROFILE_RACES];
	player->points = profile->values[MH_NET_PROFILE_POINTS];
	player->racing = (profile->flags & MH_NET_FLAG_RACING) != 0;
	player->lobby = (profile->flags & MH_NET_FLAG_LOBBY) != 0;
}

static void
load_command_state(const struct mh_net_session *session,
	struct mh_lobby_command_state *command)
{
	unsigned int i;

	command->prediction = session->config.prediction;
	command->wire = session->config.wire;
	command->host = session->host != 0;
	command->player = session->player;
	command->count = session->roster.player_count;
	command->connected = 1;
	command->quit = 0;
	command->mode =
		session->host ? session->config.mode : session->roster.mode;
	command->laps =
		session->host ? session->config.laps : session->roster.laps;
	memcpy(command->track,
		session->host ? session->config.track : session->roster.track,
		MH_NET_NAME_BYTES);
	command->track[MH_NET_NAME_BYTES] = 0;
	memcpy(command->password, session->config.password,
		MH_NET_PASSWORD_BYTES);

	for (i = 0; i < command->count; i++)
		load_command_player(
			&session->roster.players[i], &command->players[i]);

	load_command_player(
		&session->config.profile, &command->players[session->player]);
	command->players[session->player].racing = 0;
	command->players[session->player].lobby = 1;
}

static void
apply_command_values(struct mh_net_session *session,
	const struct mh_lobby_command_state *command,
	const struct mh_lobby_command_state *before,
	struct mh_net_profile *profile)
{
	const struct mh_lobby_command_player *player;
	unsigned int i;

	player = &command->players[session->player];
	*profile = session->config.profile;
	memcpy(profile->name, player->name, sizeof(profile->name));
	memcpy(profile->car, player->car, sizeof(profile->car));
	memcpy(profile->horn, player->horn, sizeof(profile->horn));
	memcpy(profile->team, player->team, sizeof(profile->team));

	for (i = 0; i < 4; i++)
		profile->values[i] = player->color[i];

	profile->values[MH_NET_PROFILE_RACES] = player->races;
	profile->values[MH_NET_PROFILE_POINTS] = player->points;
	profile->update_rate = (unsigned int)player->rate;
	profile->gear_mode = (unsigned int)player->gear;
	profile->flags =
		(profile->flags & ~(unsigned int)MH_NET_FLAG_RECORDING) |
		(player->recording ? MH_NET_FLAG_RECORDING : 0U);
	session->config.profile = *profile;
	session->config.prediction = command->prediction;
	memcpy(session->config.password, command->password,
		MH_NET_PASSWORD_BYTES);
	memcpy(session->lobby.password, command->password,
		MH_NET_PASSWORD_BYTES);

	if (session->host) {
		memcpy(session->config.track, command->track,
			MH_NET_NAME_BYTES);
		session->config.mode = (unsigned int)command->mode;
		session->config.laps = (unsigned int)command->laps;

		if (command->mode != before->mode ||
			command->laps != before->laps ||
			memcmp(command->track, before->track,
				MH_NET_NAME_BYTES)) {
			memcpy(session->roster.track, command->track,
				MH_NET_NAME_BYTES);
			session->roster.mode = session->config.mode;
			session->roster.laps = session->config.laps;
			session->roster_revision++;
		}

		for (i = 0; i < command->count; i++) {
			struct mh_net_profile *slot_profile;

			slot_profile = &session->lobby.slots[i].profile;
			slot_profile->values[MH_NET_PROFILE_RACES] =
				command->players[i].races;
			slot_profile->values[MH_NET_PROFILE_POINTS] =
				command->players[i].points;
		}
	}
}

static enum mh_error_code
send_command_text(struct mh_net_session *session,
	const struct mh_lobby_command_event *event, int echo,
	struct mh_error *error_out)
{
	struct mh_net_message message;

	if (echo)
		return mh_net_session_chat(
			session, event->text, event->color, error_out);

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	message.player = session->player;
	message.color = event->color;
	strcpy(message.text, event->text);

	return session->host
		? mh_net_session_send_audience(
			session, &message, 0, MH_NET_FLAG_LOBBY, error_out)
		: mh_net_session_queue(session, 1, &message, 1, 0, error_out);
}

static enum mh_error_code
apply_command_event(struct mh_net_session *session,
	const struct mh_net_profile *profile,
	const struct mh_lobby_command_event *event, int echo,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	switch (event->kind) {
	case MH_LOBBY_PROFILE:
		status = mh_net_session_profile(session, profile, error_out);
		if (status == MH_ERROR_NONE && !session->host &&
			session->config.wire == MH_NET_WIRE_F12) {
			session->roster.players[session->player] = *profile;
			session->roster.players[session->player].flags =
				(profile->flags & MH_NET_FLAG_RECORDING) |
				MH_NET_FLAG_LOBBY;
			session->roster_revision++;
		}

		break;

	case MH_LOBBY_ROSTER:
		status = mh_net_session_send_roster(session, error_out);
		break;

	case MH_LOBBY_KICK:
		status = mh_net_session_kick(session, event->player, error_out);
		break;

	case MH_LOBBY_SEND:
		status = send_command_text(session, event, echo, error_out);
		break;

	default:
		/* Presentation and transport actions stay with the caller. */
		break;
	}

	return status;
}

enum mh_error_code
mh_net_session_command(struct mh_net_session *session,
	struct mh_lobby_command_state *console,
	const struct mh_lobby_command_catalog *catalog, const char *text,
	struct mh_lobby_command_output *output, struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_lobby_command_state command, before;
	struct mh_lobby_command_output result;
	struct mh_net_profile profile;
	const struct mh_lobby_command_event *event;
	enum mh_error_code status;
	unsigned int i;

	if (session == NULL || console == NULL || output == NULL ||
		session->phase != MH_NET_LOBBY ||
		session->roster.player_count == 0 ||
		session->roster.player_count >
			mh_net_session_capacity(session) ||
		session->player >= session->roster.player_count ||
		(!session->host && console->confirm_clear))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby command session");

	command = *console;
	load_command_state(session, &command);
	before = command;
	status = mh_lobby_command(&command, catalog, text, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *session;
	apply_command_values(&next, &command, &before, &profile);

	for (i = 0; i < result.count; i++) {
		event = &result.events[i];
		status = apply_command_event(&next, &profile, event,
			i != 0 && result.events[i - 1].kind == MH_LOBBY_LOCAL,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (command.quit) {
		status = mh_net_session_leave(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*session = next;
	*console = command;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_toggle_ready(
	struct mh_net_session *session, struct mh_error *error_out)
{
	static const struct mh_lobby_command_catalog catalog = {0};
	struct mh_lobby_command_state console;
	struct mh_lobby_command_output output;
	struct mh_net_session next;
	unsigned int flags, count, i;
	const char *command;
	enum mh_error_code status;

	if (session == NULL || session->host ||
		session->phase != MH_NET_LOBBY ||
		session->config.wire != MH_NET_WIRE_31_LOBBY ||
		session->player == 0 ||
		session->player >= session->roster.player_count ||
		session->player >= MH_NET_WIRE_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"dedicated lobby is not ready");

	flags = session->roster.players[session->player].flags;
	count = flags & MH_NET_FLAG_PASS ? 2 : 1;
	command = flags & MH_NET_FLAG_READY ? "/notready" : "/ready";
	next = *session;
	memset(&console, 0, sizeof(console));

	for (i = 0; i < count; i++) {
		status = mh_net_session_command(
			&next, &console, &catalog, command, &output, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
apply_key_values(struct mh_net_session *session,
	const struct mh_lobby_key_values *values)
{
	struct mh_net_profile *profile;
	int changed;

	profile = &session->roster.players[session->player];
	changed = memcmp(profile->car, values->car, MH_NET_NAME_BYTES) != 0 ||
		profile->gear_mode != values->gear;
	memcpy(session->config.profile.car, values->car, MH_NET_NAME_BYTES);
	session->config.profile.gear_mode = (unsigned int)values->gear;
	memcpy(profile->car, values->car, MH_NET_NAME_BYTES);
	profile->gear_mode = (unsigned int)values->gear;

	if (session->host) {
		profile = &session->lobby.slots[session->player].profile;
		memcpy(profile->car, values->car, MH_NET_NAME_BYTES);
		profile->gear_mode = (unsigned int)values->gear;
		changed = changed ||
			memcmp(session->roster.track, values->track,
				MH_NET_NAME_BYTES) != 0 ||
			session->roster.laps != values->laps ||
			session->roster.mode != values->mode;
		memcpy(session->config.track, values->track, MH_NET_NAME_BYTES);
		memcpy(session->roster.track, values->track, MH_NET_NAME_BYTES);
		session->config.laps = session->roster.laps =
			(unsigned int)values->laps;
		session->config.mode = session->roster.mode =
			(unsigned int)values->mode;
	}

	if (changed)
		session->roster_revision++;
}

static void
load_key_controls(const struct mh_net_session *session,
	struct mh_lobby_keys *keys, int detached)
{
	unsigned int i;

	keys->host = session->host != 0;

	/*
	 * The rejected client has no assigned roster slot. Its local profile
	 * still accepts F6/F7 and paste. A one-slot local view lets the same
	 * controller retain its timers.
	 */
	keys->player = detached ? 0 : session->player;
	keys->count = detached ? 1 : session->roster.player_count;
	keys->values.gear = session->config.profile.gear_mode;
	memcpy(keys->values.car, session->config.profile.car,
		MH_NET_NAME_BYTES);
	keys->values.car[MH_NET_NAME_BYTES] = 0;
	keys->values.laps = session->host || detached ? session->config.laps
						: session->roster.laps;
	keys->values.mode = session->host || detached ? session->config.mode
						: session->roster.mode;
	memcpy(keys->values.track,
		session->host || detached ? session->config.track
					: session->roster.track,
		MH_NET_NAME_BYTES);
	keys->values.track[MH_NET_NAME_BYTES] = 0;

	for (i = 0; i < keys->count; i++) {
		keys->lobby[i] = !detached &&
			(session->roster.players[i].flags &
				MH_NET_FLAG_LOBBY) != 0;
		keys->return_pending[i] =
			!detached && (session->return_pending & (1U << i)) != 0;
	}
}

static enum mh_error_code
apply_key_action(struct mh_net_session *session,
	const struct mh_lobby_key_action *action, double now_ms,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	switch (action->kind) {
	case MH_LOBBY_KEY_PROFILE:
		status = mh_net_session_profile(
			session, &session->config.profile, error_out);
		break;

	case MH_LOBBY_KEY_ROSTER:
		status = mh_net_session_send_roster(session, error_out);
		break;

	case MH_LOBBY_KEY_START:
		status = mh_net_session_begin_race(session, now_ms, error_out);
		break;

	default:
		/* Local confirmation/wait messages remain in output. */
		break;
	}

	return status;
}

enum mh_error_code
mh_net_session_keys(struct mh_net_session *session,
	struct mh_lobby_keys *controls,
	const struct mh_lobby_key_catalog *catalog,
	const struct mh_lobby_key_input *input, double now_ms,
	struct mh_lobby_key_output *output, struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_lobby_keys keys;
	struct mh_lobby_key_output result;
	const struct mh_lobby_key_action *action;
	enum mh_error_code status;
	unsigned int i;
	int detached;

	if (!mh_net_session_clock_valid(session, now_ms) ||
		now_ms > (double)MH_U32_MAX || controls == NULL ||
		output == NULL || (!session->host && controls->confirm_clear))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby key session");

	detached = !session->host &&
		(session->phase == MH_NET_REJECTED ||
			session->phase == MH_NET_CLOSED);

	if (!detached &&
		(session->phase != MH_NET_LOBBY ||
			session->roster.player_count == 0 ||
			session->roster.player_count >
				mh_net_session_capacity(session) ||
			session->player >= session->roster.player_count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby key session");

	keys = *controls;
	load_key_controls(session, &keys, detached);

	status = mh_lobby_keys_step(&keys, catalog, input, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *session;
	next.now_ms = now_ms;

	if (detached) {
		/*
		 * Close the rejected connection. Preserve local edits and
		 * expired timers without enqueuing to a dead peer. Reconnection
		 * will publish this profile through normal admission.
		 */
		memcpy(next.config.profile.car, keys.values.car,
			MH_NET_NAME_BYTES);
		next.config.profile.gear_mode = (unsigned int)keys.values.gear;
		*session = next;
		*controls = keys;
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < result.count; i++) {
		action = &result.actions[i];
		apply_key_values(&next, &action->values);
		status = apply_key_action(&next, action, now_ms, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	apply_key_values(&next, &keys.values);
	*session = next;
	*controls = keys;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
