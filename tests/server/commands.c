#include <stdio.h>
#include <string.h>

#include "server/commands/client.h"
#include "server/commands/operator.h"
#include "server/logging/events.h"

static int failures;
static char logged[MH_SERVER_EVENT_BYTES];

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static void
capture_event(void *context, const char *text)
{
	unsigned int *count;

	count = context;
	(*count)++;
	strcpy(logged, text);
}

static void
event_format(void)
{
	struct mh_server_event_logger logger;
	char prefix[MH_SERVER_EVENT_BYTES];
	unsigned int count;

	count = 0;
	logger.write = capture_event;
	logger.context = &count;
	mh_server_event_log(&logger, "Driver", " entered the lobby.");
	check(count == 1 && strcmp(logged, "Driver entered the lobby.") == 0,
		"event concatenates original prefix and suffix");
	mh_server_event_log(&logger, "Back to lobby.", NULL);
	check(count == 2 && strcmp(logged, "Back to lobby.") == 0,
		"event permits absent suffix");
	memset(prefix, 'x', sizeof(prefix) - 2);
	prefix[sizeof(prefix) - 2] = 0;
	mh_server_event_log(&logger, prefix, "YZ");
	check(count == 3 && strlen(logged) == sizeof(logged) - 1 &&
			logged[sizeof(logged) - 2] == 'Y',
		"original console limits combined text to 255 bytes");
	mh_server_event_log(NULL, "disabled", NULL);
	logger.write = NULL;
	mh_server_event_log(&logger, "disabled", NULL);
	check(count == 3, "absent console sink produces no output");
}

static void
operator_chat(void)
{
	struct mh_server_config config;
	struct mh_server_lobby lobby;
	struct mh_server_operator_output output;
	char text[MH_SERVER_TEXT_BYTES];
	int racing;

	mh_server_config_defaults(&config);
	strcpy(config.server_name, "Local OpenMH");
	check(mh_server_lobby_init(&config, "Course", &lobby, NULL) ==
			MH_ERROR_NONE,
		"initialize console chat lobby");

	for (racing = 0; racing <= 1; racing++) {
		check(mh_server_operator_command(&lobby, &config, racing,
			"hej hej", &output, NULL) == MH_ERROR_NONE &&
				output.chat && output.text.count == 1 &&
				strcmp(output.text.lines[0],
					"console: hej hej") == 0,
			"lobby and race chat identify the console, independent of server name");
		memset(text, 'x', sizeof(text) - 1);
		text[sizeof(text) - 1] = 0;
		check(mh_server_operator_command(&lobby, &config, racing, text,
			&output, NULL) == MH_ERROR_NONE &&
				output.chat &&
				strlen(output.text.lines[0]) ==
					MH_SERVER_TEXT_BYTES - 1 &&
				strncmp(output.text.lines[0], "console: ",
					sizeof("console: ") - 1) == 0,
			"long console chat remains bounded and terminated");
	}
}

static void
track_information(void)
{
	struct mh_server_config config;
	struct mh_server_lobby lobby, saved;
	struct mh_server_ready ready;
	struct mh_server_command_output output;
	struct mh_server_operator_output console;
	struct mh_server_text text;
	struct mh_net_message message;
	unsigned int i, j, found;

	mh_server_config_defaults(&config);
	config.track_count = 2;
	strcpy(config.tracks[0].name, "Short Course");
	strcpy(config.tracks[1].name, "Long Course");
	check(mh_server_lobby_init(&config, "Short Course", &lobby, NULL) ==
			MH_ERROR_NONE,
		"initialize track listing lobby");
	lobby.peers[1] = 10;
	lobby.roster.players[1].flags = MH_NET_FLAG_LOBBY;
	strcpy(lobby.roster.players[1].name, "Driver");
	memset(&ready, 0, sizeof(ready));
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	message.player = 1;
	strcpy(message.text, "Driver: /TrAcKs");
	saved = lobby;
	mh_server_information_welcome(&lobby, &config, &text);
	check(strncmp(text.lines[0], "OpenMH Server ", 14) == 0 &&
			strlen(text.lines[0]) > 14,
		"welcome identifies the OpenMH server build");
	check(mh_server_client_command(&lobby, &ready, &config, 0, 10, &message,
		&output, NULL) == MH_ERROR_NONE &&
			output.count == 3 && !output.forward &&
			!output.refresh_roster,
		"track list is a read-only client command");
	check(strcmp(output.replies[0].text,
		"Available tracks (current: Short Course):") == 0 &&
			strcmp(output.replies[1].text,
				"Short Course, Long Course") == 0 &&
			strstr(output.replies[2].text, "/track <name>") != NULL,
		"track list includes exact configured names and selection instructions");

	for (i = 0; i < output.count; i++)
		check(output.replies[i].recipients == 2 &&
				!output.replies[i].log,
			"track listing goes only to the requesting client");

	check(memcmp(&lobby, &saved, sizeof(lobby)) == 0,
		"listing does not change track or readiness");

	config.lock_track = 1;
	check(mh_server_client_command(&lobby, &ready, &config, 1, 10, &message,
		&output, NULL) == MH_ERROR_NONE &&
			output.count == 3 &&
			strstr(output.replies[2].text, "locked") != NULL,
		"waiting players can list tracks during a race even when changes are locked");
	check(mh_server_operator_command(&lobby, &config, 1, "/tracks",
		&console, NULL) == MH_ERROR_NONE &&
			console.text.count == 3 && !console.chat &&
			!console.refresh_roster &&
			strcmp(console.text.lines[1], output.replies[1].text) ==
				0,
		"server console lists tracks without broadcasting race chat");
	check(mh_server_operator_command(&lobby, &config, 0, " /tracks ",
		&console, NULL) == MH_ERROR_NONE &&
			console.text.count == 3,
		"lobby console accepts normal command whitespace");

	config.track_count = MH_SERVER_TRACKS;

	for (i = 0; i < config.track_count; i++)
		sprintf(config.tracks[i].name, "Course%09u",
			i % MH_SERVER_TRACKS);

	mh_server_information_tracks(&lobby, &config, &text);
	check(text.count <= MH_SERVER_TEXT_LINES,
		"maximum track catalog fits the bounded reply");

	for (i = 0; i < config.track_count; i++) {
		found = 0;

		for (j = 1; j + 1 < text.count; j++)
			found += strstr(text.lines[j], config.tracks[i].name) !=
				NULL;

		check(found == 1,
			"every maximum-length track name appears exactly once");
	}
}

int
main(void)
{
	struct mh_server_config config;
	struct mh_server_lobby lobby, saved;
	struct mh_server_ready ready;
	struct mh_server_command_output output;
	struct mh_net_message message;

	event_format();
	operator_chat();
	track_information();
	mh_server_config_defaults(&config);
	check(mh_server_lobby_init(&config, "TRACK1", &lobby, NULL) ==
			MH_ERROR_NONE,
		"initialize command lobby");
	memset(&ready, 0, sizeof(ready));
	memset(&message, 0, sizeof(message));
	lobby.peers[1] = 10;
	lobby.peers[2] = 11;
	lobby.roster.players[1].flags = MH_NET_FLAG_LOBBY;
	lobby.roster.players[2].flags = MH_NET_FLAG_LOBBY;
	strcpy(lobby.roster.players[1].name, "Driver");
	message.type = MH_NET_CHAT;
	message.player = 1;
	message.color = 0x123456UL;
	strcpy(message.text, "Driver: /ReAdY ignored arguments");
	check(mh_server_client_command(&lobby, &ready, &config, 0, 10, &message,
		&output, NULL) == MH_ERROR_NONE &&
			!output.forward && output.count == 1 &&
			output.replies[0].recipients == 6 &&
			output.replies[0].color == 0xe06820UL &&
			output.replies[0].log &&
			strcmp(output.replies[0].text, "Driver is ready.") == 0,
		"ready command sends original notice to all lobby drivers including sender");
	saved = lobby;
	check(mh_server_client_command(&lobby, &ready, &config, 0, 11, &message,
		&output, NULL) == MH_ERROR_PERMISSION &&
			memcmp(&saved, &lobby, sizeof(lobby)) == 0,
		"another peer cannot issue commands for a driver");
	strcpy(message.text, "Driver: hello");
	check(mh_server_client_command(&lobby, &ready, &config, 0, 10, &message,
		&output, NULL) == MH_ERROR_NONE &&
			output.forward && output.count == 0,
		"normal chat is forwarded without server replies");
	strcpy(message.text, "Other: hello");
	check(mh_server_client_command(&lobby, &ready, &config, 0, 10, &message,
		&output, NULL) == MH_ERROR_NONE &&
			output.forward &&
			!strcmp(output.chat, "Driver: Other: hello"),
		"server attributes chat to its author");
	strcpy(message.text, "first\nsecond");
	check(mh_server_client_command(&lobby, &ready, &config, 0, 10, &message,
		&output, NULL) == MH_ERROR_INVALID,
		"chat must fit on one line");
	strcpy(message.text, "Driver: /unknown");
	check(mh_server_client_command(&lobby, &ready, &config, 0, 10, &message,
		&output, NULL) == MH_ERROR_NONE &&
			!output.forward && output.count == 1 &&
			output.replies[0].recipients == 2 &&
			!output.replies[0].log &&
			strcmp(output.replies[0].text,
				"Unknown command: /unknown") == 0,
		"unknown command response goes only to its author");
	memset(message.text, 'x', sizeof(message.text));
	check(mh_server_client_command(&lobby, &ready, &config, 0, 10, &message,
		&output, NULL) == MH_ERROR_INVALID,
		"unterminated caller text is rejected");
	message.player = 0;
	check(mh_server_client_command(&lobby, &ready, &config, 0, 10, &message,
		&output, NULL) == MH_ERROR_PERMISSION,
		"client cannot impersonate server text");

	return failures != 0;
}
