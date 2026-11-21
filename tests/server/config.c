#include <stdio.h>
#include <string.h>

#include "engine/network/protocol_profile.h"
#include "server/config/config.h"

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static void
check_defaults(void)
{
	struct mh_server_config config;
	struct mh_error error;
	size_t line;

	memset(&config, 0xff, sizeof(config));
	check(mh_server_config_parse(&config, NULL, 0, &line, &error) ==
				MH_ERROR_NONE &&
			error.message == NULL && line == 0,
		"empty configuration uses defaults");
	check(strcmp(config.session_name, "Motorhead") == 0 &&
			strcmp(config.server_name, "Server") == 0 &&
			config.password[0] == '\0' &&
			config.race_log[0] == '\0' &&
			config.start_track[0] == '\0',
		"default text");
	check(config.port == 16000 && config.clients_start_port == 0 &&
			config.clients_end_port == 0 &&
			config.max_players == 12 && config.min_ready == 1 &&
			config.max_rate == 30 &&
			config.simulation_rate == 120 &&
			config.end_countdown_seconds == 30 &&
			config.max_race_minutes == 60 &&
			config.ban_minutes == 10 &&
			config.unlocked_cars == 10 && config.laps == 10 &&
			config.lobby_timeout_seconds == 60 &&
			config.race_timeout_seconds == 30 &&
			config.start_timeout_seconds == 120,
		"binary defaults differ from shipped configuration");
	check(config.track_count == 0 && config.ban_count == 0 &&
			config.mode == MH_SERVER_NORMAL &&
			config.grid == MH_SERVER_GRID_RANDOM &&
			!config.disable_io && !config.all_ready &&
			!config.ghost_reverse && !config.ghost_all &&
			!config.step_track && !config.lock_track &&
			!config.lock_laps && !config.lock_mode &&
			!config.lock_grid,
		"default lists, rules and flags");
}

static void
check_options(void)
{
	static const char *const parts[] = {
		"// authored configuration, not an extracted resource\r\n"
		"sessionNAME Club Race ignored\r\n"
		"Password Sword Fish // comment\n"
		"ServerName Console // comment\n"
		"RaceLog /tmp/race.log\n"
		"StartTrack Goldbridge\n"
		"PortNumber 17001\nClientsStartPort 17002\nClientsEndPort 17020\n"
		"MaxPlayers 99\nMinReady 99\nMaxRate 999\n"
		"EndCountdown 45\nMaxRaceTime 20\nBanTime 2\nUnlockedCars 14\nLaps 5\n"
		"LobbyTimeOut 90\nRaceTimeOut 15\nStartTimeOut 240\n",
		"GameMode Elimination\nMode Normal\nGrid BestLap\nGrid unknown\n"
		"DisableIO\nAllReady\nGhostRev\nGhostAll\nStepTrack\n"
		"LockTrack\nLockLaps\nLockMode\nLockGrid\n"
		"Track Goldbridge data\\goldbridge.mot\nTrack Redrock /tmp/redrock.mot\n"
		"Ban 192.0.2.1\nBan 198.51.100.2\n"
		"timeout in seconds\n"};

	struct mh_server_config config;
	char text[1024];

	check(mh_server_config_parse(&config, "SimulationRate 240", 18, NULL,
		NULL) == MH_ERROR_NONE &&
			config.simulation_rate == 240,
		"native simulation rate");
	check(mh_server_config_parse(&config, "SimulationRate 0", 16, NULL,
		NULL) != MH_ERROR_NONE,
		"zero simulation rate rejected");
	check(mh_server_config_parse(&config, "SimulationRate 90", 17, NULL,
		NULL) == MH_ERROR_NONE &&
			config.simulation_rate == 90,
		"custom simulation rate accepted");
	check(mh_server_config_parse(&config, "SimulationRate 4294967295", 25,
		NULL, NULL) == MH_ERROR_NONE &&
			config.simulation_rate == 0xffffffffUL,
		"full unsigned simulation rate retained");
	check(mh_server_config_parse(&config, "SimulationRate 4294967296", 25,
		NULL, NULL) != MH_ERROR_NONE,
		"simulation rate overflow rejected");
	check(mh_server_config_parse(&config, "SimulationRate 9999999999", 25,
		NULL, NULL) != MH_ERROR_NONE,
		"excessive simulation rate rejected");
	check(mh_server_config_parse(
		&config, "NetworkProtocol legacy", 22, NULL, NULL) ==
			(mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) != NULL
					? MH_ERROR_NONE
					: MH_ERROR_UNSUPPORTED),
		"legacy protocol availability");
	check(mh_server_config_parse(&config, "NetworkProtocol unknown", 23,
		NULL, NULL) != MH_ERROR_NONE,
		"unknown network protocol rejected");
	check(mh_server_config_parse(
		&config, "NetworkProtocol openmh", 22, NULL, NULL) ==
			(mh_net_protocol_get(MH_NET_PROTOCOL_OPENMH) != NULL
					? MH_ERROR_NONE
					: MH_ERROR_UNSUPPORTED),
		"OpenMH protocol availability");
	strcpy(text, parts[0]);
	strcat(text, parts[1]);
	check(mh_server_config_parse(&config, text, strlen(text), NULL, NULL) ==
			MH_ERROR_NONE,
		"parse all original options");
	check(strcmp(config.session_name, "Club Race") == 0 &&
			strcmp(config.password, "Sword Fish") == 0 &&
			strcmp(config.server_name, "Console") == 0 &&
			strcmp(config.race_log, "/tmp/race.log") == 0 &&
			strcmp(config.start_track, "Goldbridge") == 0,
		"text and comments");
	check(config.port == 17001 && config.clients_start_port == 17002 &&
			config.clients_end_port == 17020 &&
			config.max_players == 12 && config.min_ready == 12 &&
			config.max_rate == 255 &&
			config.end_countdown_seconds == 45 &&
			config.max_race_minutes == 20 &&
			config.ban_minutes == 2 && config.unlocked_cars == 14 &&
			config.laps == 5 &&
			config.lobby_timeout_seconds == 90 &&
			config.race_timeout_seconds == 15 &&
			config.start_timeout_seconds == 240,
		"numeric values and original upper clamps");
	check(config.mode == MH_SERVER_ELIMINATION &&
			config.grid == MH_SERVER_GRID_BEST_LAP,
		"GameMode spelling and grid retention");
	check(config.disable_io && config.all_ready && config.ghost_reverse &&
			config.ghost_all && config.step_track &&
			config.lock_track && config.lock_laps &&
			config.lock_mode && config.lock_grid,
		"all flags");
	check(config.track_count == 2 &&
			strcmp(config.tracks[0].name, "Goldbridge") == 0 &&
			strcmp(config.tracks[0].path, "data\\goldbridge.mot") ==
				0 &&
			strcmp(config.tracks[1].path, "/tmp/redrock.mot") ==
				0 &&
			config.ban_count == 2 &&
			strcmp(config.bans[1], "198.51.100.2") == 0,
		"ordered course paths and bans");
}

static void
check_rules(void)
{
	static const char *const names[] = {"Random", "LastRace", "ScoreNormal",
		"ScoreReverse", "TeamRandom", "BestLap"};

	struct mh_server_config config;
	char text[128];
	unsigned int i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		sprintf(text, "Grid %s\n", names[i]);
		check(mh_server_config_parse(&config, text, strlen(text), NULL,
			NULL) == MH_ERROR_NONE &&
				(unsigned int)config.grid == i,
			"six grid choices preserve original ordinals");
	}

	strcpy(text,
		"GameMode Elimination\nGameMode unknown\nMaxPlayers 0\nLaps 26");
	check(mh_server_config_parse(&config, text, strlen(text), NULL, NULL) ==
				MH_ERROR_NONE &&
			config.mode == MH_SERVER_NORMAL &&
			config.max_players == 0 && config.laps == 26,
		"parsing retains original values; startup validation is separate");
	strcpy(text,
		"SessionName 12345678901234567890\nServerName One /comment\n");
	check(mh_server_config_parse(&config, text, strlen(text), NULL, NULL) ==
				MH_ERROR_NONE &&
			strcmp(config.session_name, "123456789012345") == 0 &&
			strcmp(config.server_name, "One") == 0,
		"original name truncation");
}

static void
check_invalid(void)
{
	static const char *const invalid[] = {"Laps", "PortNumber -1",
		"MaxRate 2x", "PortNumber 2147483648", "Track OnlyName",
		"Password // missing", "GameMode", "Ban", "Grid"};

	struct mh_server_config config;
	struct mh_error error;
	char text[2048];
	size_t line;
	unsigned int i;

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		sprintf(text, "\n%s\n", invalid[i]);
		check(mh_server_config_parse(&config, text, strlen(text), &line,
			&error) != MH_ERROR_NONE &&
				line == 2 && error.message != NULL,
			"invalid option reports its source line");
	}

	memset(text, 'x', sizeof(text));
	check(mh_server_config_parse(&config, text, sizeof(text), &line,
		NULL) == MH_ERROR_RANGE &&
			line == 1,
		"overlong line rejected");
	check(mh_server_config_parse(&config, "x\0y", 3, NULL, NULL) ==
			MH_ERROR_INVALID,
		"embedded NUL rejected");
	text[0] = '\0';

	for (i = 0; i <= MH_SERVER_BANS; i++)
		strcat(text, "Ban 192.0.2.1\n");

	check(mh_server_config_parse(&config, text, strlen(text), &line,
		NULL) == MH_ERROR_RANGE &&
			line == 21,
		"ban capacity checked");
	text[0] = '\0';

	for (i = 0; i <= MH_SERVER_TRACKS; i++)
		strcat(text, "Track Test x.mot\n");

	check(mh_server_config_parse(&config, text, strlen(text), &line,
		NULL) == MH_ERROR_RANGE &&
			line == 101,
		"course capacity checked");
}

int
main(void)
{
	check_defaults();
	check_options();
	check_rules();
	check_invalid();

	return failures != 0;
}
