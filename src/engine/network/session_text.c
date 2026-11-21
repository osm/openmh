#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/session_internal.h"

#define SYSTEM_NOTICE_COLOR 0xe06820UL

enum mh_error_code
mh_net_session_race_message(struct mh_net_session *session, const char *name,
	const char *suffix, struct mh_error *error_out)
{
	struct mh_net_message message;
	size_t name_length, suffix_length;

	name_length = strlen(name);
	suffix_length = strlen(suffix);

	if (name_length > MH_NET_TEXT_LIMIT ||
		suffix_length > MH_NET_TEXT_LIMIT - name_length)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"race notification too long");

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	message.player = 0;
	message.color = SYSTEM_NOTICE_COLOR;
	memcpy(message.text, name, name_length);
	memcpy(message.text + name_length, suffix, suffix_length + 1);
	mh_net_session_add_chat(session, &message);

	return session->host ? mh_net_session_send_audience(session, &message,
				0, MH_NET_FLAG_RACING, error_out)
			: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * Host system text uses its slot and orange, but addresses lobby members even
 * while the host itself is racing.
 */
enum mh_error_code
mh_net_session_lobby_message(struct mh_net_session *session, const char *name,
	const char *suffix, unsigned long except, int local, int remote,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	size_t name_length, suffix_length;

	name_length = strlen(name);
	suffix_length = strlen(suffix);

	if (name_length > MH_NET_TEXT_LIMIT ||
		suffix_length > MH_NET_TEXT_LIMIT - name_length)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"lobby notification too long");

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	message.player = 0;
	message.color = SYSTEM_NOTICE_COLOR;
	memcpy(message.text, name, name_length);
	memcpy(message.text + name_length, suffix, suffix_length + 1);

	if (local)
		mh_net_session_add_chat(session, &message);

	return remote ? mh_net_session_send_audience(session, &message, except,
				MH_NET_FLAG_LOBBY, error_out)
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_net_session_add_chat(
	struct mh_net_session *session, const struct mh_net_message *message)
{
	struct mh_net_chat_line *line;

	if (session->chat_count == MH_NET_CHAT_LINES) {
		memmove(session->chat, session->chat + 1,
			(MH_NET_CHAT_LINES - 1) * sizeof(session->chat[0]));
		session->chat_count--;
	}

	line = &session->chat[session->chat_count++];
	line->player = message->player;
	line->color = message->color;
	memcpy(line->text, message->text, sizeof(line->text));
	session->chat_revision++;
}

void
mh_net_session_set_notice(
	struct mh_net_session *session, const struct mh_net_message *message)
{
	if (session->notice.color == message->color &&
		strcmp(session->notice.text, message->text) == 0)
		return;

	session->notice.player = message->player;
	session->notice.color = message->color;
	strcpy(session->notice.text, message->text);
	session->notice_revision++;
}

enum mh_error_code
mh_net_session_chat(struct mh_net_session *session, const char *text,
	unsigned long color, struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_message message;
	enum mh_error_code status;
	size_t length, capacity;

	if (session == NULL || text == NULL ||
		(session->phase != MH_NET_LOBBY &&
			!mh_net_session_in_race(session)) ||
		color > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid local chat");

	length = 0;
	capacity = session->config.wire == MH_NET_WIRE_F12
		? MH_NET_PACKET_LIMIT - MH_NET_CHAT_OVERHEAD + 1
		: MH_NET_CHAT_BYTES;

	while (length < capacity && text[length] != 0)
		length++;

	if (length == capacity)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "chat text too long");

	next = *session;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	message.player = session->player;
	message.color = color;
	memcpy(message.text, text, length);
	mh_net_session_add_chat(&next, &message);
	status = session->host
		? mh_net_session_send_audience(&next, &message, 0,
			next.lobby.slots[0].profile.flags &
				MH_NET_FLAG_PRESENT,
			error_out)
		: mh_net_session_queue(&next, 1, &message, 1, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_notice(struct mh_net_session *session, unsigned int player,
	const char *text, unsigned long color, struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_message message;
	enum mh_error_code status;

	if (session == NULL || !session->host || session->player != 0 ||
		!mh_net_session_in_race(session) || player >= MH_NET_PLAYERS ||
		text == NULL || strlen(text) >= sizeof(session->notice.text) ||
		color > MH_U32_MAX ||
		session->lobby.slots[player].phase != MH_NET_IN_RACE)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid host notice");

	next = *session;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_NOTICE;
	message.player = session->player;
	message.color = color;
	strcpy(message.text, text);

	if (player == session->player) {
		mh_net_session_set_notice(&next, &message);
	} else {
		status = mh_net_session_queue(&next,
			next.lobby.slots[player].peer, &message, 1, 0,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_announcement(struct mh_net_session *session, unsigned long peer,
	struct mh_net_batch *batch, const struct mh_net_message *message,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (batch->size + strlen(message->text) + MH_NET_CHAT_OVERHEAD >
		MH_NET_BATCH_LIMIT) {
		status = mh_net_session_queue_batch(
			session, peer, batch, 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_net_batch_reset(batch);
	}

	return mh_net_wire_batch_append(
		session->config.wire, batch, message, error_out);
}

static enum mh_error_code
deliver_announcements(struct mh_net_session *session, unsigned int player,
	const struct mh_net_announcement *events, unsigned int count,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	struct mh_net_batch batch;
	unsigned int i;
	enum mh_error_code status;

	mh_net_batch_reset(&batch);

	for (i = 0; i < count; i++) {
		if (!(events[i].recipients & (1U << player)))
			continue;

		memset(&message, 0, sizeof(message));
		message.type = events[i].chat ? MH_NET_CHAT : MH_NET_NOTICE;
		message.color = events[i].color;
		strcpy(message.text, events[i].text);

		if (player == session->player) {
			if (events[i].chat)
				mh_net_session_add_chat(session, &message);
			else
				mh_net_session_set_notice(session, &message);
		} else {
			status = append_announcement(session,
				session->lobby.slots[player].peer, &batch,
				&message, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	if (player != session->player)
		return mh_net_session_queue_batch(session,
			session->lobby.slots[player].peer, &batch, 1,
			error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_announcements(struct mh_net_session *session,
	const struct mh_net_announcement *events, unsigned int count,
	struct mh_error *error_out)
{
	struct mh_net_session next;
	unsigned int active_player_mask, i;
	enum mh_error_code status;

	if (session == NULL || !session->host || session->player != 0 ||
		!mh_net_session_in_race(session) ||
		count > MH_NET_ANNOUNCEMENTS || (count != 0 && events == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race announcements");

	active_player_mask = 0;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		if (session->lobby.slots[i].phase == MH_NET_IN_RACE)
			active_player_mask |= 1U << i;

	for (i = 0; i < count; i++) {
		if (!events[i].recipients ||
			(events[i].recipients & ~active_player_mask) ||
			events[i].color > MH_U32_MAX ||
			(events[i].chat != 0 && events[i].chat != 1) ||
			memchr(events[i].text, 0, sizeof(events[i].text)) ==
				NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid race announcement");
	}

	next = *session;

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		if (!(active_player_mask & (1U << i)))
			continue;

		status = deliver_announcements(
			&next, i, events, count, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
