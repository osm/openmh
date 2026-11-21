#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/simulation_rate.h"
#include "engine/core/text.h"
#include "engine/network/protocol_profile.h"
#include "server/config/config.h"

enum {
	CONFIG_LINE_BYTES = 255,
	CONFIG_TOKENS = 3,
	CONFIG_MAX_RATE = 255
};

struct number_option {
	const char	*name;
	size_t		offset;
	unsigned long	maximum;
};

struct text_option {
	const char	*name;
	size_t		offset;
	size_t		capacity;
};

struct flag_option {
	const char	*name;
	size_t		offset;
};

static const struct number_option number_options[] = {
	{"PortNumber", offsetof(struct mh_server_config, port), MH_U32_MAX},
	{"ClientsStartPort",
		offsetof(struct mh_server_config, clients_start_port),
		MH_U32_MAX},
	{"ClientsEndPort", offsetof(struct mh_server_config, clients_end_port),
		MH_U32_MAX},
	{"MaxPlayers", offsetof(struct mh_server_config, max_players),
		MH_SERVER_RACERS},
	{"MinReady", offsetof(struct mh_server_config, min_ready),
		MH_SERVER_RACERS},
	{"MaxRate", offsetof(struct mh_server_config, max_rate),
		CONFIG_MAX_RATE},
	{"EndCountdown",
		offsetof(struct mh_server_config, end_countdown_seconds),
		MH_U32_MAX},
	{"MaxRaceTime", offsetof(struct mh_server_config, max_race_minutes),
		MH_U32_MAX},
	{"BanTime", offsetof(struct mh_server_config, ban_minutes), MH_U32_MAX},
	{"UnlockedCars", offsetof(struct mh_server_config, unlocked_cars),
		MH_U32_MAX},
	{"Laps", offsetof(struct mh_server_config, laps), MH_U32_MAX},
	{"LobbyTimeOut",
		offsetof(struct mh_server_config, lobby_timeout_seconds),
		MH_U32_MAX},
	{"RaceTimeOut", offsetof(struct mh_server_config, race_timeout_seconds),
		MH_U32_MAX},
	{"StartTimeOut",
		offsetof(struct mh_server_config, start_timeout_seconds),
		MH_U32_MAX}};

static const struct text_option text_options[] = {
	{"SessionName", offsetof(struct mh_server_config, session_name),
		MH_SERVER_NAME_BYTES},
	{"Password", offsetof(struct mh_server_config, password),
		MH_SERVER_NAME_BYTES},
	{"RaceLog", offsetof(struct mh_server_config, race_log),
		MH_SERVER_PATH_BYTES},
	{"ServerName", offsetof(struct mh_server_config, server_name),
		MH_SERVER_NAME_BYTES},
	{"StartTrack", offsetof(struct mh_server_config, start_track),
		MH_SERVER_NAME_BYTES}};

static const struct flag_option flag_options[] = {
	{"DisableIO", offsetof(struct mh_server_config, disable_io)},
	{"AllReady", offsetof(struct mh_server_config, all_ready)},
	{"GhostRev", offsetof(struct mh_server_config, ghost_reverse)},
	{"GhostAll", offsetof(struct mh_server_config, ghost_all)},
	{"StepTrack", offsetof(struct mh_server_config, step_track)},
	{"LockTrack", offsetof(struct mh_server_config, lock_track)},
	{"LockLaps", offsetof(struct mh_server_config, lock_laps)},
	{"LockMode", offsetof(struct mh_server_config, lock_mode)},
	{"LockGrid", offsetof(struct mh_server_config, lock_grid)}};

static const char *const grid_names[] = {"Random", "LastRace", "ScoreNormal",
	"ScoreReverse", "TeamRandom", "BestLap"};

void
mh_server_config_defaults(struct mh_server_config *config)
{
	/* Server 3.1 defaults; sample files override these. */
	memset(config, 0, sizeof(*config));
	strcpy(config->session_name, "Motorhead");
	strcpy(config->server_name, "Server");
	config->port = MH_NET_LANINET_PORT;
	config->max_players = MH_SERVER_RACERS;
	config->min_ready = 1;
	config->max_rate = 30;
	config->simulation_rate = MH_SIM_RATE_DEFAULT;
	config->network_legacy =
		mh_net_protocol_default() == MH_NET_PROTOCOL_LEGACY;
	config->end_countdown_seconds = 30;
	config->max_race_minutes = 60;
	config->ban_minutes = 10;
	config->unlocked_cars = 10;
	config->laps = 10;
	config->lobby_timeout_seconds = 60;
	config->race_timeout_seconds = 30;
	config->start_timeout_seconds = 120;
}

static int
is_space(unsigned char byte)
{
	return byte == ' ' || (byte >= '\t' && byte <= '\r');
}

static unsigned int
split_tokens(char *line, char *tokens[CONFIG_TOKENS])
{
	unsigned int count;

	count = 0;

	while (*line != '\0' && count < CONFIG_TOKENS) {
		while (is_space((unsigned char)*line))
			line++;

		if (*line == '\0')
			break;

		tokens[count++] = line;

		while (*line != '\0' && !is_space((unsigned char)*line))
			line++;

		if (*line != '\0')
			*line++ = '\0';
	}

	return count;
}

static int
is_comment(const char *token)
{
	/* The original tests either of the first two bytes for '/'. */
	return token[0] == '/' || (token[0] != '\0' && token[1] == '/');
}

static void
copy_text(char *output, size_t capacity, const char *first, const char *second)
{
	size_t length;

	length = 0;

	while (*first != '\0' && length + 1 < capacity)
		output[length++] = *first++;

	if (second != NULL && length + 1 < capacity) {
		output[length++] = ' ';

		while (*second != '\0' && length + 1 < capacity)
			output[length++] = *second++;
	}

	output[length] = '\0';
}

static enum mh_error_code
parse_number(const char *text, unsigned long limit, unsigned long *output,
	struct mh_error *error_out)
{
	unsigned long value, digit;

	/* Native policy: reject negative/malformed/overflowing settings. */
	if (*text == '+')
		text++;

	if (*text == '\0')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing server configuration number");

	value = 0;

	while (*text != '\0') {
		if (*text < '0' || *text > '9')
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid server configuration number");

		digit = (unsigned long)(*text++ - '0');

		if (value > (limit - digit) / 10)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"server configuration number exceeds its limit");

		value = value * 10 + digit;
	}

	*output = value;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
parse_option(struct mh_server_config *config, char *tokens[CONFIG_TOKENS],
	unsigned int count, struct mh_error *error_out)
{
	const char *second;
	struct mh_server_track *track;
	enum mh_error_code status;
	unsigned long value;
	size_t i;
	char *base;

	base = (char *)config;

	if (mh_ascii_equal(tokens[0], "NetworkProtocol")) {
		if (count < 2)
			goto missing_value;

		if (!mh_ascii_equal(tokens[1], "openmh") &&
			!mh_ascii_equal(tokens[1], "legacy"))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"NetworkProtocol must be legacy or openmh");

		if (mh_net_protocol_get(mh_ascii_equal(tokens[1], "legacy")
				? MH_NET_PROTOCOL_LEGACY
				: MH_NET_PROTOCOL_OPENMH) == NULL)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"requested network protocol is not included in this build");

		config->network_legacy = mh_ascii_equal(tokens[1], "legacy");

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mh_ascii_equal(tokens[0], "SimulationRate")) {
		if (count < 2)
			goto missing_value;

		status = parse_number(
			tokens[1], MH_SIM_RATE_MAX, &value, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!mh_sim_rate_valid(value))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"SimulationRate must be an integer from 1 to 4294967295 Hz");

		config->simulation_rate = value;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < sizeof(flag_options) / sizeof(flag_options[0]); i++) {
		if (!mh_ascii_equal(tokens[0], flag_options[i].name))
			continue;

		*(int *)(base + flag_options[i].offset) = 1;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < sizeof(number_options) / sizeof(number_options[0]);
		i++) {
		if (!mh_ascii_equal(tokens[0], number_options[i].name))
			continue;

		if (count < 2)
			goto missing_value;

		value = 0;
		status = parse_number(tokens[1], MH_I32_MAX, &value, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (value > number_options[i].maximum)
			value = number_options[i].maximum;

		*(unsigned long *)(base + number_options[i].offset) = value;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < sizeof(text_options) / sizeof(text_options[0]); i++) {
		if (!mh_ascii_equal(tokens[0], text_options[i].name))
			continue;

		if (count < 2 || strncmp(tokens[1], "//", 2) == 0)
			goto missing_value;

		second = count > 2 && !is_comment(tokens[2]) ? tokens[2] : NULL;
		copy_text(base + text_options[i].offset,
			text_options[i].capacity, tokens[1], second);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mh_ascii_equal(tokens[0], "Track")) {
		if (count < 3 || strncmp(tokens[1], "//", 2) == 0 ||
			strncmp(tokens[2], "//", 2) == 0)
			goto missing_value;

		if (config->track_count == MH_SERVER_TRACKS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many server tracks");

		track = &config->tracks[config->track_count++];
		copy_text(track->name, sizeof(track->name), tokens[1], NULL);
		copy_text(track->path, sizeof(track->path), tokens[2], NULL);
	} else if (mh_ascii_equal(tokens[0], "Ban")) {
		if (count < 2 || strncmp(tokens[1], "//", 2) == 0)
			goto missing_value;

		if (config->ban_count == MH_SERVER_BANS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many server bans");

		if (mh_text_copy(config->bans[config->ban_count],
			MH_SERVER_BAN_ADDRESS_BYTES, tokens[1],
			error_out) != MH_ERROR_NONE)
			return MH_ERROR_RANGE;

		config->ban_count++;
	} else if (mh_ascii_equal(tokens[0], "GameMode")) {
		if (count < 2 || strncmp(tokens[1], "//", 2) == 0)
			goto missing_value;

		config->mode = mh_ascii_equal(tokens[1], "Elimination")
			? MH_SERVER_ELIMINATION
			: MH_SERVER_NORMAL;
	} else if (mh_ascii_equal(tokens[0], "Grid")) {
		if (count < 2 || strncmp(tokens[1], "//", 2) == 0)
			goto missing_value;

		for (i = 0; i < sizeof(grid_names) / sizeof(grid_names[0]);
			i++) {
			if (mh_ascii_equal(tokens[1], grid_names[i]))
				config->grid = (enum mh_server_grid)i;
		}
	}

	/* Ignore unrecognized configuration keys. */
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

missing_value:

	return mh_error_set(error_out, MH_ERROR_INVALID,
		"missing server configuration value");
}

enum mh_error_code
mh_server_config_parse(struct mh_server_config *config, const char *text,
	size_t size, size_t *error_line, struct mh_error *error_out)
{
	char line[CONFIG_LINE_BYTES], *tokens[CONFIG_TOKENS];
	size_t offset, start, length, line_number;
	unsigned int count;
	enum mh_error_code status;

	if (error_line != NULL)
		*error_line = 0;

	if (config == NULL || (text == NULL && size != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid server configuration input");

	mh_server_config_defaults(config);
	offset = 0;
	line_number = 0;

	while (offset < size) {
		line_number++;

		if (error_line != NULL)
			*error_line = line_number;

		start = offset;

		while (offset < size && text[offset] != '\n')
			offset++;

		length = offset - start;

		if (length != 0 && text[start + length - 1] == '\r')
			length--;

		if (length >= sizeof(line))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"server configuration line too long");

		if (memchr(text + start, '\0', length) != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"NUL in server configuration");

		memcpy(line, text + start, length);
		line[length] = '\0';

		if (offset < size)
			offset++;

		count = split_tokens(line, tokens);

		if (count == 0 || is_comment(tokens[0]))
			continue;

		status = parse_option(config, tokens, count, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (error_line != NULL)
		*error_line = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_net_protocol *
mh_server_config_protocol(const struct mh_server_config *config)
{
	return config == NULL ? NULL
			: mh_net_protocol_get(config->network_legacy
						? MH_NET_PROTOCOL_LEGACY
						: MH_NET_PROTOCOL_OPENMH);
}
