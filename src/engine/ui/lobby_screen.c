#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/lobby_screen.h"

#define PLAYER_PALETTE_FIRST 243

enum mh_error_code
mh_lobby_screen_palette(const struct mh_lobby_roster *roster,
	unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_error *error_out)
{
	unsigned int i, index;
	unsigned long color;

	if (roster == NULL || palette == NULL || roster->count == 0 ||
		roster->count > MH_LOBBY_ROSTER_PLAYERS ||
		roster->player >= roster->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby palette state");

	for (i = 0; i < roster->count; i++)
		if (roster->players[i].racing || roster->players[i].lobby ||
			i == roster->player) {
			color = roster->players[i].color;
			index = (PLAYER_PALETTE_FIRST + i) * 3;
			palette[index] = (unsigned char)((color >> 16) & 255);
			palette[index + 1] =
				(unsigned char)((color >> 8) & 255);
			palette[index + 2] = (unsigned char)(color & 255);
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_lobby_screen_init(struct mh_lobby_screen *screen)
{
	memset(screen, 0, sizeof(*screen));
	mh_messages_init(&screen->messages);
	mh_messages_width(&screen->messages, MH_LOBBY_CONSOLE_WIDTH, NULL);
}

void
mh_lobby_screen_enter(struct mh_lobby_screen *screen)
{
	memset(&screen->keys, 0, sizeof(screen->keys));
	screen->console.confirm_clear = screen->quit = 0;
	screen->reconnect = screen->statistics = 0;
	mh_messages_width(&screen->messages, MH_LOBBY_CONSOLE_WIDTH, NULL);
}

enum mh_error_code
mh_lobby_screen_message(struct mh_lobby_screen *screen, const char *text,
	unsigned long color, unsigned long message_ms,
	const unsigned int advance[MH_FONT_GLYPHS], struct mh_error *error_out)
{
	if (screen == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing lobby screen");

	return mh_messages_add(&screen->messages, text, NULL, color, message_ms,
		advance, error_out);
}

enum mh_error_code
mh_lobby_screen_collect(struct mh_lobby_screen *screen,
	const struct mh_net_session *session, unsigned long message_ms,
	const unsigned int advance[MH_FONT_GLYPHS], struct mh_error *error_out)
{
	struct mh_messages next_messages;
	const struct mh_net_chat_line *line;
	unsigned long pending;
	unsigned int i;
	enum mh_error_code status;

	if (screen == NULL || session == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing lobby collection state");

	pending = session->chat_revision - screen->chat_revision;

	if (pending > session->chat_count)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"lobby chat history was not drained");

	if (!pending)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	next_messages = screen->messages;

	for (i = session->chat_count - (unsigned int)pending;
		i < session->chat_count; i++) {
		line = &session->chat[i];
		status = mh_messages_add(&next_messages, line->text, NULL,
			line->color, message_ms, advance, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	screen->messages = next_messages;
	screen->chat_revision = session->chat_revision;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_hotkeys(struct mh_lobby_screen *screen, struct mh_net_session *session,
	struct mh_chat_history *history,
	const struct mh_lobby_key_catalog *keys,
	const struct mh_lobby_key_input *input,
	const unsigned int advance[MH_FONT_GLYPHS], double transport_ms,
	unsigned long message_ms, struct mh_error *error_out)
{
	struct mh_lobby_key_output output;
	enum mh_error_code status;
	unsigned int i;

	memcpy(screen->keys.draft, history->rows[0].text, MH_CHAT_TEXT_BYTES);
	screen->keys.confirm_clear = screen->console.confirm_clear;
	status = mh_net_session_keys(session, &screen->keys, keys, input,
		transport_ms, &output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(history->rows[0].text, screen->keys.draft, MH_CHAT_TEXT_BYTES);
	screen->console.confirm_clear = screen->keys.confirm_clear;

	for (i = 0; i < output.count; i++)
		if (output.actions[i].kind == MH_LOBBY_KEY_CONFIRM ||
			output.actions[i].kind == MH_LOBBY_KEY_WAIT) {
			status = mh_lobby_screen_message(screen,
				output.actions[i].text, 0xe06820UL, message_ms,
				advance, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

	return mh_lobby_screen_collect(
		screen, session, message_ms, advance, error_out);
}

enum mh_error_code
mh_lobby_screen_password_input(struct mh_lobby_screen *screen,
	struct mh_net_session *session, struct mh_chat_history *history,
	const struct mh_lobby_key_catalog *keys,
	const struct mh_lobby_key_input *input,
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[MH_FONT_GLYPHS], double transport_ms,
	unsigned long message_ms, struct mh_error *error_out)
{
	struct mh_lobby_screen next_screen;
	struct mh_net_session next_session;
	struct mh_chat_history edited_history;
	enum mh_error_code status;

	if (screen == NULL || session == NULL || history == NULL ||
		input == NULL || input->events == NULL || session->host ||
		session->phase != MH_NET_REJECTED ||
		session->rejection_reason != 2 ||
		!(transport_ms >= 0 && transport_ms <= (double)MH_U32_MAX) ||
		message_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby password input");

	next_screen = *screen;
	next_session = *session;
	edited_history = *history;
	status = mh_lobby_password_edit(
		(unsigned char *)next_session.config.password, input->events,
		characters, advance, &next_screen.reconnect, error_out);

	/*
	 * Request reconnect before falling into paste/F6/F7. The connection
	 * owner consumes that request after committing local input. Password
	 * editing bypasses chat submission, recall and scrolling.
	 */
	if (status == MH_ERROR_NONE)
		status = apply_hotkeys(&next_screen, &next_session,
			&edited_history, keys, input, advance, transport_ms,
			message_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*screen = next_screen;
	*session = next_session;
	*history = edited_history;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
dispatch_command(struct mh_lobby_screen *screen, struct mh_net_session *session,
	const struct mh_lobby_command_catalog *commands, const char *text,
	const unsigned int advance[MH_FONT_GLYPHS], unsigned long message_ms,
	struct mh_error *error_out)
{
	struct mh_lobby_command_output output;
	const struct mh_lobby_command_event *event;
	enum mh_error_code status;
	unsigned int i;

	status = mh_net_session_command(
		session, &screen->console, commands, text, &output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < output.count; i++) {
		event = &output.events[i];

		if (event->kind == MH_LOBBY_LOCAL) {
			if (i + 1 < output.count &&
				output.events[i + 1].kind == MH_LOBBY_SEND)
				status =
					mh_lobby_screen_collect(screen, session,
						message_ms, advance, error_out);
			else
				status = mh_lobby_screen_message(screen,
					event->text, event->color, message_ms,
					advance, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		if (event->kind == MH_LOBBY_RECONNECT)
			screen->reconnect = 1;
	}

	if (screen->console.quit)
		screen->quit = 1;

	return status;
}

enum mh_error_code
mh_lobby_screen_input(struct mh_lobby_screen *screen,
	struct mh_net_session *session, struct mh_chat_history *history,
	const struct mh_lobby_command_catalog *commands,
	const struct mh_lobby_key_catalog *keys,
	const struct mh_lobby_key_input *input,
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[MH_FONT_GLYPHS], double transport_ms,
	unsigned long message_ms, struct mh_error *error_out)
{
	struct mh_lobby_screen next_screen;
	struct mh_net_session next_session;
	struct mh_chat_history edited_history;
	struct mh_lobby_submission submission;
	enum mh_error_code status;

	if (screen == NULL || session == NULL || history == NULL ||
		input == NULL || input->events == NULL ||
		!(transport_ms >= 0 && transport_ms <= (double)MH_U32_MAX) ||
		message_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby screen input");

	next_screen = *screen;
	next_session = *session;
	edited_history = *history;
	next_screen.reconnect = 0;
	status = mh_lobby_screen_collect(
		&next_screen, &next_session, message_ms, advance, error_out);

	if (status == MH_ERROR_NONE) {
		if (next_session.config.wire == MH_NET_WIRE_31_LOBBY)
			status = mh_lobby_console_edit_patched(&edited_history,
				next_session.host,
				input->held != NULL
					? (input->held[MH_KEY_TAB] &
						MH_KEY_DOWN_BIT) != 0
					: input->events[MH_KEY_TAB] != 0,
				input->events, characters, advance, &submission,
				error_out);
		else
			status = mh_lobby_console_edit(&edited_history,
				next_session.host, input->events, characters,
				advance, &submission, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	next_screen.statistics = submission.statistics;

	if (submission.ready) {
		status = dispatch_command(&next_screen, &next_session, commands,
			(const char *)submission.text, advance, message_ms,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (!next_screen.quit && !next_screen.reconnect) {
		status = mh_lobby_console_scroll(&next_screen.scroll,
			&next_screen.messages, input->events, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = apply_hotkeys(&next_screen, &next_session,
			&edited_history, keys, input, advance, transport_ms,
			message_ms, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*screen = next_screen;
	*session = next_session;
	*history = edited_history;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
