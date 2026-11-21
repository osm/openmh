#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/input/lobby_console.h"
#include "engine/input/text_edit.h"

enum command_role {
	HOST_ONLY,
	CLIENT_ONLY,
	BOTH_ROLES
};

struct completion_command {
	const char		*text;
	enum command_role	role;
};

static int
ascii_space(unsigned int byte)
{
	return byte == 32 || (byte >= 9 && byte <= 13);
}

static int
command_matches(const unsigned char *token, size_t length, const char *command)
{
	/* A completion prefix cannot extend into arguments or past NUL. */
	if (length > strcspn(command, " "))
		return 0;

	return mh_ascii_equal_n((const char *)token, command, length);
}

enum mh_error_code
mh_lobby_complete(unsigned char text[MH_CHAT_TEXT_BYTES], int host,
	struct mh_error *error_out)
{

	static const struct completion_command commands[] = {
		{"/track ", HOST_ONLY},
		{"/gamemode ", HOST_ONLY},
		{"/laps ", HOST_ONLY},
		{"/kick ", HOST_ONLY},
		{"/rate ", CLIENT_ONLY},
		{"/reconnect", CLIENT_ONLY},
		{"/name ", BOTH_ROLES},
		{"/nick ", BOTH_ROLES},
		{"/team ", BOTH_ROLES},
		{"/car ", BOTH_ROLES},
		{"/horn ", BOTH_ROLES},
		{"/gear ", BOTH_ROLES},
		{"/color ", BOTH_ROLES},
		{"/carcolor1 ", BOTH_ROLES},
		{"/carcolor2 ", BOTH_ROLES},
		{"/carcolor3 ", BOTH_ROLES},
		{"/password ", BOTH_ROLES},
		{"/minpredict ", BOTH_ROLES},
		{"/maxpredict ", BOTH_ROLES},
		{"/modem", BOTH_ROLES},
		{"/ISDN", BOTH_ROLES},
		{"/T1", BOTH_ROLES},
		{"/LAN", BOTH_ROLES},
		{"/record ", BOTH_ROLES},
		{"/commands", BOTH_ROLES},
		{"/help", BOTH_ROLES},
		{"/quit", BOTH_ROLES},
		{"/exit", BOTH_ROLES},
		{"/bye", BOTH_ROLES},
		{"/me ", BOTH_ROLES},
	};

	size_t begin, end;
	unsigned int i;

	if (text == NULL || memchr(text, 0, MH_CHAT_TEXT_BYTES) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby completion text");

	begin = 0;

	while (ascii_space(text[begin]))
		begin++;

	if (text[begin] != '/')
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	end = begin;

	while (text[end] && !ascii_space(text[end]))
		end++;

	if (end - begin < 2)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		if ((host && commands[i].role == CLIENT_ONLY) ||
			(!host && commands[i].role == HOST_ONLY))
			continue;

		if (command_matches(
			text + begin, end - begin, commands[i].text)) {
			strcpy((char *)text, commands[i].text);
			break;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lobby_complete_patched(unsigned char text[MH_CHAT_TEXT_BYTES], int host,
	int *matched, struct mh_error *error_out)
{
	static const char *const common[] = {"/name ", "/nick ", "/team ",
		"/car ", "/horn ", "/gear ", "/color ", "/carcolor1 ",
		"/carcolor2 ", "/carcolor3 ", "/password ", "/minpredict ",
		"/maxpredict ", "/modem", "/ISDN", "/T1", "/LAN", "/commands",
		"/help", "/quit", "/exit", "/bye", "/me "};

	static const char *const server[] = {
		"/track ", "/gamemode ", "/laps ", "/kick "};

	static const char *const client[] = {"/msg ", "/msgteam ", "/msgrace ",
		"/clear", "/mode", "/laps ", "/lapdown", "/lapup", "/track ",
		"/trackdown", "/trackup", "/tracks", "/status", "/notready",
		"/ready", "/helpserver", "/grid", "/kick ", "/users", "/rate ",
		"/connect ", "/reconnect"};

	const char *command;
	size_t begin, end, i, count, common_count, role_count;

	if (text == NULL || matched == NULL ||
		memchr(text, 0, MH_CHAT_TEXT_BYTES) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid patched lobby completion text");

	*matched = 0;
	begin = 0;

	while (ascii_space(text[begin]))
		begin++;

	if (text[begin] != '/')
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	end = begin;

	while (text[end] && !ascii_space(text[end]))
		end++;

	if (end - begin < 2)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	common_count = sizeof(common) / sizeof(common[0]);
	role_count = host ? sizeof(server) / sizeof(server[0])
			: sizeof(client) / sizeof(client[0]);

	count = common_count + role_count;

	for (i = 0; i <= count; i++) {
		if (i < common_count)
			command = common[i];
		else if (i < count)
			command = host ? server[i - common_count]
				: client[i - common_count];
		else
			command = "/record ";

		if (command_matches(text + begin, end - begin, command)) {
			strcpy((char *)text, command);
			*matched = 1;
			break;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
console_edit(struct mh_chat_history *history, int host, int patched,
	int tab_down, const unsigned char events[MH_KEY_COUNT],
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[256], struct mh_lobby_submission *submission,
	struct mh_error *error_out)
{
	struct mh_chat_history next;
	struct mh_lobby_submission out;
	struct mh_text_edit_output edited;
	enum mh_error_code status;
	unsigned int i;
	int matched;

	if (history == NULL || events == NULL || characters == NULL ||
		advance == NULL || submission == NULL ||
		history->recall >= MH_CHAT_HISTORY_ROWS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lobby editor");

	for (i = 0; i < MH_CHAT_HISTORY_ROWS; i++)
		if (memchr(history->rows[i].text, 0, MH_CHAT_TEXT_BYTES) ==
				NULL ||
			history->rows[i].words[0] > MH_U32_MAX ||
			history->rows[i].words[1] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby history");

	next = *history;
	memset(&out, 0, sizeof(out));
	status = mh_text_edit(next.rows[0].text, MH_CHAT_TEXT_BYTES,
		MH_CHAT_EDIT_LIMIT, MH_CHAT_EDIT_WIDTH, events, characters,
		advance, &edited, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (events[MH_KEY_ENTER]) {
		out.ready = 1;
		strcpy((char *)out.text, (const char *)next.rows[0].text);
		mh_chat_history_update(&next, MH_CHAT_STORE, NULL);
		mh_chat_history_update(&next, MH_CHAT_RESET_RECALL, NULL);
	}

	if (patched && tab_down) {
		mh_lobby_complete_patched(
			next.rows[0].text, host, &matched, NULL);
		out.statistics = !host && !matched;
	} else if (!patched && events[MH_KEY_TAB])
		mh_lobby_complete(next.rows[0].text, host, NULL);

	if (events[MH_KEY_UP])
		mh_chat_history_update(&next, MH_CHAT_OLDER, NULL);

	if (events[MH_KEY_DOWN])
		mh_chat_history_update(&next, MH_CHAT_NEWER, NULL);

	*history = next;
	*submission = out;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lobby_console_edit(struct mh_chat_history *history, int host,
	const unsigned char events[MH_KEY_COUNT],
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[256], struct mh_lobby_submission *submission,
	struct mh_error *error_out)
{
	return console_edit(history, host, 0, 0, events, characters, advance,
		submission, error_out);
}

enum mh_error_code
mh_lobby_console_edit_patched(struct mh_chat_history *history, int host,
	int tab_down, const unsigned char events[MH_KEY_COUNT],
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[256], struct mh_lobby_submission *submission,
	struct mh_error *error_out)
{
	return console_edit(history, host, 1, tab_down, events, characters,
		advance, submission, error_out);
}

enum mh_error_code
mh_lobby_password_edit(unsigned char text[MH_LOBBY_PASSWORD_BYTES],
	const unsigned char events[MH_KEY_COUNT],
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[256], int *connect,
	struct mh_error *error_out)
{
	struct mh_text_edit_output edited;
	enum mh_error_code status;

	if (connect == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby password output");

	status = mh_text_edit(text, MH_LOBBY_PASSWORD_BYTES,
		MH_LOBBY_PASSWORD_LIMIT, MH_CHAT_EDIT_WIDTH, events, characters,
		advance, &edited, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*connect = events[MH_KEY_ENTER] != 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
