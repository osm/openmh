#include <stdio.h>
#include <string.h>

#include "client/session/network_race.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/game/mode.h"
#include "engine/storage/recording_path.h"
#include "interfaces/storage/data_directory.h"

#define MESSAGE_COLOR 0xe06820UL

enum {
	MESSAGE_MARGIN = 10,
	MESSAGE_MAX_WIDTH = 540,
	FRAME_EVENT_LIMIT = 64,
	RECORDING_NOTICE_STARTED = 1,
	RECORDING_NOTICE_FAILED = 2
};

struct chat_frame {
	struct mh_network_race	*controller;
	unsigned long		milliseconds;
};

static int
starts_with(const char *text, const char *match)
{
	return mh_ascii_equal_n(text, match, strlen(match));
}

static void
apply_race_notice(struct mh_network_race *controller,
	const struct mh_net_session *session)
{
	if (session->roster.mode != MH_NET_MODE_ELIMINATION ||
		session->notice_revision ==
			controller->race->field.race_notice_revision)
		return;

	controller->race->field.race_notice_revision = session->notice_revision;
	controller->race->field.race_notice = MH_RACE_NOTICE_NONE;

	if (starts_with(session->notice.text, "You are the winner!"))
		controller->race->field.race_notice = MH_RACE_NOTICE_WIN;

	if (starts_with(session->notice.text, "You have been eliminated by"))
		controller->race->field.race_notice = MH_RACE_NOTICE_OUT;
}

static long double
message_clock_ms(const struct mh_network_race *controller)
{
	return mh_drive_clock_ms(&controller->race->field.cars[0]);
}

static enum mh_error_code
add_message_at(struct mh_network_race *controller, const char *text,
	const char *suffix, unsigned long color, unsigned long milliseconds,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_messages_add(&controller->messages, text, suffix, color,
		milliseconds, controller->race_context->hud.message_advance,
		error_out);

	if (status == MH_ERROR_NONE)
		controller->has_messages = 1;

	return status;
}

static enum mh_error_code
add_message(void *context, const char *text, const char *suffix,
	unsigned long color, struct mh_error *error_out)
{
	struct mh_network_race *controller;
	long double now;

	controller = (struct mh_network_race *)context;
	now = message_clock_ms(controller);

	if (!(now >= 0 && now < MH_U32_MODULUS))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network message clock");

	return add_message_at(
		controller, text, suffix, color, (unsigned long)now, error_out);
}

static enum mh_error_code
collect_chat(void *context, struct mh_error *error_out)
{
	const struct chat_frame *frame;
	struct mh_network_race *controller;
	const struct mh_net_session *session;
	const struct mh_net_chat_line *line;
	enum mh_error_code status;
	unsigned long pending;
	unsigned int i;

	frame = (const struct chat_frame *)context;
	controller = frame->controller;
	session = mh_network_session(controller->network);
	pending = session->chat_revision - controller->chat_revision;

	if (pending > session->chat_count)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network chat history was not drained");

	for (i = session->chat_count - (unsigned int)pending;
		i < session->chat_count; i++) {
		line = &session->chat[i];
		status = add_message_at(controller, line->text, NULL,
			line->color, frame->milliseconds, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		controller->chat_revision++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_race_record(struct mh_network_race *controller,
	const struct mh_user_data *data, const char *asset_root,
	const char *directory, const char *command, struct mh_error *error_out)
{
	struct mh_net_session *session;
	struct mh_error error;
	enum mh_error_code status;
	struct mh_data_directory *target;
	const char *filename;

	if (controller == NULL || controller->network == NULL ||
		controller->recording != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network recording controller");

	session = mh_network_session(controller->network);

	if (!(session->config.profile.flags & MH_NET_FLAG_RECORDING))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	target = NULL;
	filename = NULL;
	status = mh_recording_network_path_from(
		data, directory, command, controller->recording_path, &error);

	if (status == MH_ERROR_NONE)
		status = mh_recording_destination_open_from(data, asset_root,
			controller->recording_path, &target, &filename, &error);

	if (status == MH_ERROR_NONE)
		status = mh_demo_recording_open_network_in(target, filename,
			controller->race, session, &controller->recording,
			&error);

	if (status != MH_ERROR_NONE) {
		mh_data_directory_close(target);
		fprintf(stderr, "Network recording unavailable: %s\n",
			error.message);
		return add_message(controller,
			"Recording unavailable: ", error.message, MESSAGE_COLOR,
			error_out);
	}

	printf("Network recording opened: %s\n", controller->recording_path);
	fflush(stdout);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_race_record_end(struct mh_network_race *controller, int save,
	struct mh_error *error_out)
{
	struct mh_error error;
	enum mh_error_code status;
	int saved, failed;

	if (controller == NULL || controller->recording == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!save) {
		mh_demo_recording_abort(controller->recording);
		controller->recording = NULL;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	failed = mh_demo_recording_write_error(controller->recording, NULL) !=
		MH_ERROR_NONE;
	status = mh_demo_recording_finish(controller->recording,
		&controller->race->field, &saved, &error);
	controller->recording = NULL;

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Network recording not saved: %s\n",
			error.message);
		return failed
			? mh_error_set(error_out, MH_ERROR_NONE, NULL)
			: add_message(controller,
				"Recording could not be saved: ",
				error.message, MESSAGE_COLOR, error_out);
	}

	printf("Network recording %s: %s\n", saved ? "saved" : "too short",
		controller->recording_path);
	fflush(stdout);

	return add_message(controller,
		saved ? "Race recording saved."
		: "Race recording was too short to save.",
		NULL, MESSAGE_COLOR, error_out);
}

static enum mh_error_code
publish_recording_notices(
	struct mh_network_race *controller, struct mh_error *error_out)
{
	struct mh_error diagnostic;

	enum mh_error_code status;

	if (controller->recording == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mh_demo_recording_start_ticks(controller->recording) != 0 &&
		!(controller->recording_notices & RECORDING_NOTICE_STARTED)) {
		status = add_message(controller, "Race recording started!",
			NULL, MESSAGE_COLOR, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		controller->recording_notices |= RECORDING_NOTICE_STARTED;
		printf("Network recording started: %s\n",
			controller->recording_path);
		fflush(stdout);
	}

	if (mh_demo_recording_write_error(controller->recording, NULL) !=
			MH_ERROR_NONE &&
		!(controller->recording_notices & RECORDING_NOTICE_FAILED)) {
		mh_demo_recording_write_error(
			controller->recording, &diagnostic);
		fprintf(stderr, "Network recording disabled: %s\n",
			diagnostic.message);
		status = add_message(controller,
			"Error! Could not write demo file!", NULL,
			MESSAGE_COLOR, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		controller->recording_notices |= RECORDING_NOTICE_FAILED;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
drain_chat(struct chat_frame *frame, unsigned int limit,
	struct mh_error *error_out)
{
	struct mh_network_issue issue;
	enum mh_error_code status;

	status = mh_network_drain_observed(frame->controller->network, limit,
		&issue, collect_chat, frame, error_out);

	if (status == MH_ERROR_NONE && issue.reason.code != MH_ERROR_NONE)
		fprintf(stderr, "Peer %lu: %s\n", issue.peer,
			issue.reason.message);

	return status;
}

static enum mh_error_code
send_chat(void *context, const char *text, unsigned long color,
	unsigned long milliseconds, struct mh_error *error_out)
{
	struct mh_network_race *controller;
	struct chat_frame frame;
	enum mh_error_code status;

	controller = (struct mh_network_race *)context;

	if (milliseconds > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network chat clock");

	frame.controller = controller;
	frame.milliseconds = milliseconds;
	status = collect_chat(&frame, error_out);

	/*
	 * Ten simultaneous shortcuts can fan out to 70 packets. Flush between
	 * sends so the bounded transport outbox cannot reject a valid chord.
	 */
	if (status == MH_ERROR_NONE)
		status = drain_chat(&frame, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_net_session_chat(mh_network_session(controller->network),
		text, color, error_out);

	if (status == MH_ERROR_NONE)
		status = collect_chat(&frame, error_out);

	if (status == MH_ERROR_NONE)
		status = drain_chat(&frame, 1, error_out);

	return status;
}

int
mh_network_race_ready(
	const struct mh_net_session *session, unsigned int players)
{
	unsigned int i, j, count;

	if (session == NULL || !session->host ||
		session->phase != MH_NET_LOBBY || session->return_pending ||
		players < 1 || players > MH_NET_PLAYERS)
		return 0;

	count = 0;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		if (session->lobby.slots[i].phase == MH_NET_IN_LOBBY)
			count++;

	if (count < players)
		return 0;

	for (i = 0; i < MH_NET_CONNECTIONS; i++) {
		if (session->connections[i].peer == 0 ||
			session->connections[i].closing)
			continue;

		for (j = 1; j < MH_NET_PLAYERS; j++)
			if (session->lobby.slots[j].phase == MH_NET_IN_LOBBY &&
				session->lobby.slots[j].peer ==
					session->connections[i].peer)
				break;

		if (j == MH_NET_PLAYERS)
			return 0;
	}

	return 1;
}

/* Real-time service is independent of zero/multiple gameplay ticks. */
static enum mh_error_code
service_race(void *context, int *done, struct mh_error *error_out)
{
	struct mh_network_race *controller;
	struct mh_net_session *session;
	struct chat_frame frame;
	enum mh_error_code status;

	controller = context;
	*done = 0;
	frame.controller = controller;
	frame.milliseconds = (unsigned long)message_clock_ms(controller);
	status = collect_chat(&frame, error_out);

	if (status == MH_ERROR_NONE)
		status = drain_chat(&frame, FRAME_EVENT_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	session = mh_network_session(controller->network);

	if (session->phase == MH_NET_LOBBY || session->phase == MH_NET_LOADING)
		controller->returned_to_lobby = *done = 1;
	else if (session->phase == MH_NET_CLOSED)
		return mh_error_set(error_out, MH_ERROR_STATE,
			session->kicked ? "removed from the network game"
					: "network game disconnected");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
advance_world(struct mh_network_race *controller,
	struct mh_net_session *session, const struct mh_engine_input *controls,
	long double elapsed, unsigned int *steps, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (mh_demo_recording_active(controller->recording)) {
		status = mh_demo_recording_network_advance(
			controller->recording, session, controls, elapsed,
			mh_network_now(controller->network),
			mh_field_viewed(&controller->race->field), steps,
			error_out);

		if (status != MH_ERROR_NONE &&
			mh_demo_recording_write_error(
				controller->recording, NULL) != MH_ERROR_NONE)
			status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	} else
		status = mh_net_race_frame(controller->race, session, controls,
			elapsed, mh_network_now(controller->network),
			mh_field_viewed(&controller->race->field), steps,
			error_out);

	return status;
}

static enum mh_error_code
advance_race(void *context, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct mh_network_race *controller;
	struct mh_net_session *session;
	struct mh_drive_frame sampled;
	struct chat_frame frame;
	enum mh_error_code status;

	controller = (struct mh_network_race *)context;
	*steps = 0;
	*done = 0;

	if (action != MH_FRAME_RUN)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network frame cannot be suspended by a menu");

	/*
	 * Original clock sampling precedes network service. Preview here so
	 * message publication uses that frame while physics remains atomic.
	 */
	status = mh_drive_frame_sample(
		&controller->race->field.cars[0], elapsed, &sampled, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	frame.controller = controller;
	frame.milliseconds =
		(unsigned long)((long double)sampled.seconds * 1000.0L);
	status = collect_chat(&frame, error_out);

	if (status == MH_ERROR_NONE &&
		!controller->race->field.cars[0].frame.fixed_rate)
		status = drain_chat(&frame, FRAME_EVENT_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	session = mh_network_session(controller->network);

	if (session->phase == MH_NET_LOBBY ||
		session->phase == MH_NET_LOADING) {
		status = mh_field_clock_advance(
			&controller->race->field, elapsed, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		controller->returned_to_lobby = *done = 1;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (session->phase == MH_NET_CLOSED)
		return session->kicked
			? mh_error_set(error_out, MH_ERROR_STATE,
				"removed from the network game")
			: mh_error_set(error_out, MH_ERROR_STATE,
				"network game disconnected");

	if (session->phase != MH_NET_WAITING && session->phase != MH_NET_RACING)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network race is no longer active");

	status = advance_world(
		controller, session, controls, elapsed, steps, error_out);

	if (status == MH_ERROR_NONE)
		status = publish_recording_notices(controller, error_out);

	if (status == MH_ERROR_NONE)
		status = collect_chat(&frame, error_out);

	if (status == MH_ERROR_NONE)
		apply_race_notice(controller, session);

	if (status == MH_ERROR_NONE)
		controller->race_context->results.left_mask =
			((1U << controller->race->field.count) - 1) &
			~controller->race->field.active_mask;

	if (status == MH_ERROR_NONE && session->phase == MH_NET_LOBBY)
		controller->returned_to_lobby = *done = 1;

	return status;
}

static enum mh_error_code
return_to_lobby(void *context, struct mh_error *error_out)
{
	struct mh_network_race *controller;
	struct mh_net_session *session;

	controller = (struct mh_network_race *)context;
	session = mh_network_session(controller->network);

	return mh_net_race_return(controller->race, session, error_out);
}

static int
client_progress(const struct mh_net_session *session, unsigned int player,
	struct mh_net_progress *progress)
{
	if (session->host)
		return 0;

	if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
		!(session->replication.status_valid & (1U << player))) {
		/* The setup cache is used before the first type 15. */
		progress->position = session->roster.players[player].value_a;
		progress->lap = session->roster.players[player].value_b;
		return 1;
	}

	if (session->config.wire == MH_NET_WIRE_F12 &&
		session->replication.status_revision == 0)
		return 0;

	progress->position = session->replication.status[player].value_a;
	progress->lap = session->replication.status[player].value_b;

	return 1;
}

static unsigned int
finish_position(void *context, unsigned int fallback)
{
	const struct mh_network_race *controller;
	struct mh_net_progress progress;
	const struct mh_net_session *session;

	controller = context;
	session = mh_network_session(controller->network);

	return client_progress(session, session->player, &progress)
		? progress.position
		: fallback;
}

static void
apply_host_standings(const struct mh_network_race *controller,
	const struct mh_net_session *session, struct mh_hud_state *state)
{
	struct mh_net_progress progress;
	unsigned int i, player, minimum, maximum;

	minimum = session->config.wire == MH_NET_WIRE_31_LOBBY ? 0 : 1;
	maximum = state->cars;

	if (session->config.wire == MH_NET_WIRE_31_LOBBY)
		maximum = session->roster.player_count > MH_NET_PLAYERS
			? session->roster.player_count
			: MH_NET_PLAYERS;

	player = controller->race->layout
			.player[mh_field_viewed(&controller->race->field)];

	if (client_progress(session, player, &progress)) {
		/*
		 * Original host position/lap cache is presentation only. Never
		 * push delayed status bytes into local timing/checkpoint state.
		 */
		if (progress.position >= minimum &&
			progress.position <= maximum)
			state->position = progress.position;

		if (progress.lap >= minimum && progress.lap <= state->laps + 1)
			state->lap = progress.lap;
	}

	for (i = 0; i < state->marker_count; i++) {
		player = controller->race->layout.player[i];

		if (client_progress(session, player, &progress) &&
			progress.position >= minimum &&
			progress.position <= maximum)
			state->markers[i].rank = progress.position;
	}
}

static void
apply_racer_info(const struct mh_network_race *controller,
	const struct mh_net_session *session, struct mh_hud_state *state)
{
	const struct mh_net_profile *profile;
	struct mh_race_info_row *row;
	struct mh_net_progress progress;
	unsigned int i, player;

	for (i = 0; state->show_info && i < state->info.count; i++) {
		row = &state->info.rows[i];
		player = controller->race->layout.player[row->entry];
		profile = &session->roster.players[player];
		row->automatic = profile->gear_mode;
		row->control_type = profile->control_type;
		memcpy(row->name, profile->name, sizeof(profile->name));
		row->name[sizeof(profile->name)] = 0;
		memcpy(row->team, profile->team, sizeof(profile->team));
		row->team[sizeof(profile->team)] = 0;
		row->color = profile->values[MH_NET_PROFILE_COLOR];
		row->ping = session->replication.status[player].ping_ms;

		if (client_progress(session, player, &progress))
			row->position = progress.position;
	}
}

static void
apply_hud(void *context, struct mh_hud_state *state)
{
	struct mh_network_race *controller;
	const struct mh_net_session *session;

	controller = (struct mh_network_race *)context;
	session = mh_network_session(controller->network);
	state->status_flags =
		mh_hud_status_flags(state->status_flags, MH_GAME_NETWORK, 0,
			mh_demo_recording_active(controller->recording));
	state->messages =
		controller->has_messages ? &controller->messages : NULL;

	/*
	 * Integer expiry deadlines permit truncation before their comparisons.
	 * Truncate extended precision first, avoiding a rounded-up double.
	 */
	state->message_ms = (double)(unsigned long)message_clock_ms(controller);
	state->chat_text = controller->race_hooks.chat->active
		? (const char *)controller->race_hooks.chat->history.rows[0]
			.text
		: NULL;
	state->chat_ms = (unsigned long)state->message_ms;

	if (session->phase == MH_NET_WAITING)
		state->message = "WAITING FOR RACERS";
	else if ((!state->finished ||
			session->roster.mode == MH_NET_MODE_ELIMINATION) &&
		session->notice_revision != 0 && session->notice.text[0] != 0)
		state->message = session->notice.text;

	apply_host_standings(controller, session, state);
	apply_racer_info(controller, session, state);
}

enum mh_error_code
mh_network_race_init(struct mh_network *network, struct mh_net_race *race,
	struct mh_race_context *context, struct mh_network_race *output,
	struct mh_error *error_out)
{
	struct mh_net_session *session;
	unsigned int message_width;

	session = mh_network_session(network);

	if (session == NULL || race == NULL || context == NULL ||
		output == NULL || !race->field.network ||
		!session->replication.configured ||
		(session->phase != MH_NET_WAITING &&
			session->phase != MH_NET_RACING) ||
		context->results.count != race->field.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid interactive network race");

	memset(output, 0, sizeof(*output));
	output->network = network;
	output->race = race;
	output->race_context = context;
	output->race_hooks.context = output;
	output->race_hooks.advance = advance_race;
	output->race_hooks.service = service_race;
	output->race_hooks.network_host = session->host;
	output->race_hooks.restart = return_to_lobby;
	output->race_hooks.hud = apply_hud;
	output->race_hooks.finish_position = finish_position;
	output->chat_revision = session->chat_revision;
	mh_messages_init(&output->messages);
	message_width = context->scene.assets.view_width - MESSAGE_MARGIN;

	if (message_width > MESSAGE_MAX_WIDTH)
		message_width = MESSAGE_MAX_WIDTH;

	mh_messages_width(&output->messages, message_width, NULL);
	output->chat_config.mode = MH_GAME_NETWORK;
	output->chat_config.color = session->roster.players[session->player]
					.values[MH_NET_PROFILE_COLOR];
	memcpy(output->chat_config.name,
		session->roster.players[session->player].name,
		sizeof(session->roster.players[session->player].name));
	output->race_hooks.chat_config = &output->chat_config;
	output->race_hooks.chat = mh_network_chat_state(network);
	output->race_hooks.chat->active = output->race_hooks.chat->cancelled =
		0;
	output->race_hooks.send_chat = send_chat;
	output->race_hooks.message = add_message;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
