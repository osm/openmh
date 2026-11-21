#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/input/control_type.h"
#include "server/commands/information.h"

static const char *const operator_help[] = {"", "/quit, Quit server",
	"/kick <player number>, Kick player, get player number with '/status'",
	"/password <password name>, Set password",
	"/laps <laps>, Change number of laps",
	"/gamemode <mode>, Change gamemode", "/track <track>, Change track",
	"/tracks, List available tracks", "/name <new name>, Change your name",
	"/clear, Force score clearing",
	"/user <player number>, Get extra user info",
	"/status, Get game status and player numbers",
	"/ready, Force race start", ""};

static const char *const client_help[] = {
	"/msg <PlayerName> <Message>, Send message to player",
	"/msgteam <Message>, Send message to team",
	"/msgrace <Message>, Send message to race players",
	"/kick, <playernumber>, Vote for kicking of a player",
	"/users, List players and playernumbers", "/grid, Toggle gridmode",
	"/clear, Vote for score clearing", "/mode, Toggle gamemode",
	"/lapdown, Toggle lap down", "/lapup, Toggle lap up",
	"/laps <laps>, Change number of laps", "/trackdown, Toggle track down",
	"/trackup, Toggle track up", "/track <trackname>, Change track",
	"/tracks, List available tracks", "/status, Get game status",
	"/notready, You are NOT ready to race",
	"/ready, You are ready to race"};

enum {
	TRACKS_PER_LINE = 6
};

void
mh_server_information_help(int operator_console, struct mh_server_text *text)
{
	const char *const *lines;
	unsigned int i, count;

	lines = operator_console ? operator_help : client_help;
	count = operator_console
		? sizeof(operator_help) / sizeof(operator_help[0])
		: sizeof(client_help) / sizeof(client_help[0]);

	memset(text, 0, sizeof(*text));

	for (i = 0; i < count; i++)
		strcpy(text->lines[text->count++], lines[i]);
}

void
mh_server_information_tracks(const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, struct mh_server_text *text)
{

	unsigned int i;
	char *line;

	memset(text, 0, sizeof(*text));
	line = text->lines[text->count++];
	(void)mh_text_copy(line, MH_SERVER_TEXT_BYTES,
		"Available tracks (current: ", NULL);
	(void)mh_text_append(
		line, MH_SERVER_TEXT_BYTES, lobby->roster.track, NULL);
	(void)mh_text_append(line, MH_SERVER_TEXT_BYTES, "):", NULL);

	/* Six bounded names per line fit all 100 tracks plus both notices. */
	for (i = 0; i < config->track_count; i++) {
		if (i % TRACKS_PER_LINE == 0)
			line = text->lines[text->count++];
		else
			(void)mh_text_append(
				line, MH_SERVER_TEXT_BYTES, ", ", NULL);
		(void)mh_text_append(line, MH_SERVER_TEXT_BYTES,
			config->tracks[i].name, NULL);
	}

	(void)mh_text_copy(text->lines[text->count++], MH_SERVER_TEXT_BYTES,
		config->lock_track
			? "Track changes are locked by the server."
			: "Use /track <name>, /trackup or /trackdown.",
		NULL);
}

static void
append_flag(char *line, const char *name, int enabled)
{
	if (!enabled)
		return;

	strcat(line, line[0] ? ", " : "Flags: ");
	strcat(line, name);
}

static void
status_flags(const struct mh_server_config *config, char *line)
{
	line[0] = 0;
	append_flag(line, "AllReady", config->all_ready);
	append_flag(line, "GhostRev", config->ghost_reverse);
	append_flag(line, "GhostAll", config->ghost_all);
	append_flag(line, "StepTrack", config->step_track);
	append_flag(line, "LockTrack", config->lock_track);
	append_flag(line, "LockLaps", config->lock_laps);
	append_flag(line, "LockMode", config->lock_mode);
	append_flag(line, "LockGrid", config->lock_grid);
}

void
mh_server_information_welcome(const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, struct mh_server_text *text)
{
	/* Include an empty flags line when none are set. */
	memset(text, 0, sizeof(*text));

	sprintf(text->lines[text->count++], "OpenMH Server %.*s",
		(int)(MH_SERVER_TEXT_BYTES - sizeof("OpenMH Server ")),
		MH_BUILD_VERSION);
	sprintf(text->lines[text->count++], "MinReady: %lu, UnlockedCars: %u",
		config->min_ready, lobby->roster.car_count);
	status_flags(config, text->lines[text->count++]);
	strcpy(text->lines[text->count++],
		"Type '/helpserver' for server commands and '/ready' to race!");
}

void
mh_server_information_status(const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, int operator_console,
	struct mh_server_text *text)
{
	static const char *const grids[] = {"Random", "LastRace", "ScoreNormal",
		"ScoreReverse", "TeamRandom", "BestLap"};

	const struct mh_net_profile *profile;
	unsigned int i;

	memset(text, 0, sizeof(*text));
	sprintf(text->lines[text->count++], "MaxPlayers: %lu",
		config->max_players);
	sprintf(text->lines[text->count++], "MaxRaceTime: %lu min",
		config->max_race_minutes);
	sprintf(text->lines[text->count++], "EndCountdown: %lu sec",
		config->end_countdown_seconds);
	sprintf(text->lines[text->count++], "UnlockedCars: %u",
		lobby->roster.car_count);
	sprintf(text->lines[text->count++], "MinReady: %lu", config->min_ready);
	sprintf(text->lines[text->count++], "MaxRate: %lu", config->max_rate);
	sprintf(text->lines[text->count++], "GridMode: %s", grids[lobby->grid]);
	status_flags(config, text->lines[text->count++]);

	if (!operator_console)
		return;

	for (i = 1; i < lobby->roster.player_count; i++) {
		profile = &lobby->roster.players[i];

		if (!(profile->flags & MH_NET_FLAG_LOBBY))
			continue;

		sprintf(text->lines[text->count++], "%u. %s is %sready.", i,
			profile->name,
			profile->flags & MH_NET_FLAG_READY ? "" : "NOT ");
	}
}

static int
control_letter(unsigned int control)
{
	switch (control) {
	case MH_CONTROL_KEYBOARD:
		return 'K';
	case MH_CONTROL_MOUSE:
		return 'M';
	case MH_CONTROL_JOYSTICK:
		return 'J';
	default:
		/*
		 * Native policy for unclassified controls; original leaves
		 * stale text.
		 */
		return '?';
	}
}

void
mh_server_information_users(
	const struct mh_server_lobby *lobby, struct mh_server_text *text)
{
	const struct mh_net_profile *profile;
	unsigned int i;

	memset(text, 0, sizeof(*text));

	for (i = 1; i < lobby->roster.player_count; i++) {
		profile = &lobby->roster.players[i];

		if (!(profile->flags & MH_NET_FLAG_PRESENT))
			continue;

		sprintf(text->lines[text->count++],
			"%u. %s, Rate: %u, Control: %c, Horn: %s%s", i,
			profile->name, profile->update_rate,
			control_letter(profile->control_type), profile->horn,
			profile->flags & MH_NET_FLAG_RECORDING
				? ", Recording..."
				: "");
	}
}

static long
signed_word(unsigned long value)
{
	return value > MH_I32_MAX ? -1L - (long)(MH_U32_MAX - value)
				: (long)value;
}

void
mh_server_information_user(const struct mh_server_lobby *lobby,
	unsigned int player, const struct mh_transport_address *address,
	struct mh_server_text *text)
{
	static const char *const colors[] = {"Color:      %06lx",
		"CarColor1:  %06lx", "CarColor2:  %06lx", "CarColor3:  %06lx"};

	const struct mh_net_profile *profile;
	unsigned int i;

	profile = &lobby->roster.players[player];
	memset(text, 0, sizeof(*text));
	text->count++;
	sprintf(text->lines[text->count++], "Name:       %s", profile->name);
	sprintf(text->lines[text->count++], "Team:       %s", profile->team);
	sprintf(text->lines[text->count++], "Car:        %s", profile->car);
	sprintf(text->lines[text->count++], "Horn:       %s", profile->horn);
	sprintf(text->lines[text->count++], "Gear:       %s",
		profile->gear_mode ? "Automatic" : "Manual");

	for (i = MH_NET_PROFILE_COLOR; i <= MH_NET_PROFILE_CAR_COLOR3; i++)
		sprintf(text->lines[text->count++], colors[i],
			profile->values[i]);

	sprintf(text->lines[text->count++], "Races:      %ld",
		signed_word(profile->values[MH_NET_PROFILE_RACES]));
	sprintf(text->lines[text->count++], "Score:      %ld",
		signed_word(profile->values[MH_NET_PROFILE_POINTS]));
	sprintf(text->lines[text->count++], "Rate:       %u",
		profile->update_rate);
	sprintf(text->lines[text->count++], "Ping:       %ld",
		signed_word(lobby->ping_ms[player]));

	switch (profile->control_type) {
	case MH_CONTROL_KEYBOARD:
		strcpy(text->lines[text->count++], "Control:    Keyboard");
		break;

	case MH_CONTROL_MOUSE:
		strcpy(text->lines[text->count++], "Control:    Mouse");
		break;

	case MH_CONTROL_JOYSTICK:
		strcpy(text->lines[text->count++], "Control:    Joystick");
		break;
	}

	sprintf(text->lines[text->count++], "Address:    %s:%u", address->host,
		address->port);

	if (profile->flags & MH_NET_FLAG_RACING)
		strcpy(text->lines[text->count++], "Player in game.");

	if (profile->flags & MH_NET_FLAG_LOBBY)
		strcpy(text->lines[text->count++], "Player in lobby.");

	if (profile->flags & MH_NET_FLAG_RECORDING)
		strcpy(text->lines[text->count++],
			"Player recording the race.");

	if (profile->flags & MH_NET_FLAG_UNAVAILABLE)
		strcpy(text->lines[text->count++], "Player is a ghost.");

	text->count++;
}
