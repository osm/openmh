#include <stdio.h>
#include <string.h>

#include "engine/core/arguments.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/core/text.h"
#include "engine/game/race_limits.h"
#include "engine/settings/av_internal.h"
#include "engine/settings/config_internal.h"
#include "engine/settings/parse.h"

#define JOIN_BYTES 1024
#define LOOKUP_BYTES 32
#define HORN_SEED_BYTES 4
#define SHORT_KEY_PREFIX_BYTES 8
#define CD_TRACK_PREFIX_BYTES 8

struct config_reader {
	struct mh_settings			settings;
	const struct mh_settings_catalog	*catalog;
	unsigned long				random;
};

static int
text_valid(const char *text, size_t maximum)
{
	size_t i;

	if (text == NULL)
		return 0;

	for (i = 0; i <= maximum; i++) {
		if (text[i] == 0)
			return 1;

		if ((unsigned char)text[i] < 32)
			return 0;
	}

	return 0;
}

static int
names_valid(const struct mh_settings_names *names, size_t maximum, int optional)
{
	unsigned int i;

	if (names->count > MH_SETTINGS_CATALOG_MAX ||
		(!optional && names->count == 0) ||
		(names->count != 0 && names->names == NULL))
		return 0;

	for (i = 0; i < names->count; i++)
		if (!text_valid(names->names[i], maximum) ||
			names->names[i][0] == 0)
			return 0;

	return 1;
}

enum mh_error_code
mh_settings_init(struct mh_settings *settings, const char *player_name,
	unsigned int effects_volume, unsigned int music_volume,
	unsigned int renderer, struct mh_error *error_out)
{
	struct mh_settings next;
	enum mh_error_code status;

	if (settings == NULL ||
		!text_valid(player_name, MH_SETTINGS_PLAYER_BYTES - 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid settings initialization");

	memset(&next, 0, sizeof(next));
	status = mh_av_settings_init(
		&next.av, effects_volume, music_volume, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_text_copy(next.player_name, sizeof(next.player_name),
		player_name, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	strcpy(next.control_name, "Keyboard");
	next.player_color = MH_CFG_RGB_MAX;
	next.car_colors[0] = 0x201040UL;
	next.car_colors[1] = 0x401020UL;
	next.car_colors[2] = 0xd0d0d0UL;
	next.automatic = 1;
	next.difficulty = 1;
	next.laps = 5;

	/* The original default's spelling. */
	strcpy(next.league_name, "Defualt");
	next.renderer = renderer;
	strcpy(next.session_name, "Session1");
	next.tcp_rate = MH_SETTINGS_TCP_RATE_DEFAULT;
	next.prediction.minimum = MH_NET_PREDICTION_MIN_DEFAULT;
	next.prediction.maximum = MH_NET_PREDICTION_MAX_DEFAULT;
	next.serial_port = next.baud_rate = next.flow_control = 2;
	strcpy(next.hiscore_path, "HiScores\\");
	next.menu_cd_track = 3;
	next.menu_confirmation = 1;
	*settings = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
join_tokens(char **tokens, unsigned int count, char *text, size_t capacity)
{
	size_t at, length;
	unsigned int i;

	at = 0;

	for (i = 1; i < count && at + 1 < capacity; i++) {
		if (i > 1)
			text[at++] = ' ';

		length = strlen(tokens[i]);

		if (length > capacity - 1 - at)
			length = capacity - 1 - at;

		memcpy(text + at, tokens[i], length);
		at += length;
	}

	text[at] = 0;
}

static int
find_name(const struct mh_settings_names *names, const char *value,
	unsigned int *selection)
{
	unsigned int i;

	for (i = 0; i < names->count; i++) {
		if (mh_ascii_equal(value, names->names[i])) {
			*selection = i;
			return 1;
		}
	}

	return 0;
}

static enum mh_error_code
read_color(char **tokens, unsigned int count, unsigned long *color,
	struct mh_error *error_out)
{
	unsigned long packed, whole;
	double numeric;
	unsigned int i;

	packed = 0;

	for (i = 1; i < count && i <= MH_AV_COLOR_CHANNELS; i++) {
		if (!mh_cfg_number(tokens[i], &numeric) ||
			!mh_cfg_integer(numeric, &whole))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid settings color");

		packed |= (whole & MH_CFG_COLOR_BYTE) << (24 - i * 8);
	}

	*color = packed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
select_horn(struct config_reader *reader, const char *name,
	struct mh_error *error_out)
{
	unsigned long seed;
	unsigned int i;

	if (find_name(&reader->catalog->horns, name, &reader->settings.horn))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (strlen(name) < HORN_SEED_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"unknown settings horn name is too short");

	seed = 0;

	for (i = 0; i < HORN_SEED_BYTES; i++)
		seed |= (unsigned long)(unsigned char)name[i] << (i * 8);

	reader->random = seed;
	reader->settings.horn =
		mh_random_next(&reader->random) % reader->catalog->horns.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_course_music(struct mh_settings *settings, const char *key,
	const char *value, struct mh_error *error_out)
{
	char expected[32];
	unsigned int i;
	unsigned long division, track;
	long signed_division;
	double number;

	for (i = 0; i < settings->course_music_count; i++) {
		division = settings->course_music[i].division;
		signed_division = division <= (unsigned long)MH_I32_MAX
			? (long)division
			: -1L - (long)(MH_U32_MAX - division);
		sprintf(expected, "CDTrack_%ld", signed_division);

		if (!mh_ascii_equal(key, expected))
			continue;

		if (!mh_cfg_number(value, &number) ||
			!mh_cfg_integer(number, &track))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid course music track");

		/* Clamp the unsigned converted word to 2..11. */
		if (track < MH_CFG_CD_TRACK_MIN)
			settings->course_music[i].track = MH_CFG_CD_TRACK_MIN;
		else if (track > MH_CFG_CD_TRACK_MAX)
			settings->course_music[i].track = MH_CFG_CD_TRACK_MAX;
		else
			settings->course_music[i].track = track;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
parse_line(char **tokens, unsigned int count, void *context,
	struct mh_error *error_out)
{
	static const char *const toggle[] = {"Off", "On"};
	static const char *const gearbox[] = {"Manual", "Auto"};
	static const char *const modes[] = {
		"Single", "League", "TimeAttack", "GhostMode"};

	static const char *const difficulty[] = {"Easy", "Medium", "Hard"};
	static const unsigned long baud[] = {
		38400UL, 56000UL, 57600UL, 115200UL, 128000UL, 256000UL};

	static const char *const protocol[] = {
		"IPX", "TCP", "DialUp", "Serial"};

	static const char *const parity[] = {"None", "Odd", "Even", "Mark"};
	static const char *const stops[] = {"1", "2", "1-5"};
	static const char *const flow[] = {
		"NONE", "DTR", "RTSDTR", "RTS", "XONXOFF"};

	static const char *const confirmation[] = {"Off", "On", "NoExit"};
	struct config_reader *reader;
	struct mh_settings *settings;
	const char *key;
	const char *value;
	enum mh_error_code status;
	char joined[JOIN_BYTES], key_prefix[SHORT_KEY_PREFIX_BYTES + 1];
	unsigned long packed = 0, whole;
	double numeric;
	unsigned int i;
	size_t length;

	if (count < 2)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	reader = (struct config_reader *)context;
	settings = &reader->settings;
	key = tokens[0];
	value = tokens[1];
	length = strlen(key);

	if (mh_ascii_equal_n(key, "CDTrack_", CD_TRACK_PREFIX_BYTES))
		return read_course_music(settings, key, value, error_out);

	if (mh_ascii_equal(key, "PlayerName"))
		join_tokens(tokens, count, settings->player_name,
			sizeof(settings->player_name));
	else if (mh_ascii_equal(key, "TeamName"))
		join_tokens(tokens, count, settings->team_name,
			sizeof(settings->team_name));
	else if (mh_ascii_equal(key, "ControlName")) {
		if (mh_text_copy(settings->control_name,
			sizeof(settings->control_name), value,
			NULL) != MH_ERROR_NONE)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"control name too long");
	} else if (mh_ascii_equal(key, "PlayerColour") ||
		(length == 10 &&
			(mh_ascii_equal(key, "CarColour1") ||
				mh_ascii_equal(key, "CarColour2") ||
				mh_ascii_equal(key, "CarColour3")))) {
		status = read_color(tokens, count, &packed, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (mh_ascii_equal(key, "PlayerColour"))
			settings->player_color = packed;
		else
			settings->car_colors[(unsigned int)(key[9] - '1')] =
				packed;
	} else if (mh_ascii_equal(key, "CustomCarColours"))
		mh_cfg_choice(value, toggle, sizeof(toggle) / sizeof(toggle[0]),
			&settings->custom_car_colors);
	else if (mh_ascii_equal(key, "CarTransmission"))
		mh_cfg_choice(value, gearbox,
			sizeof(gearbox) / sizeof(gearbox[0]),
			&settings->automatic);
	else if (mh_ascii_equal(key, "CarName") ||
		mh_ascii_equal(key, "TrackName") ||
		mh_ascii_equal(key, "CarHorn")) {
		join_tokens(tokens, count, joined, LOOKUP_BYTES);

		if (mh_ascii_equal(key, "CarName"))
			find_name(
				&reader->catalog->cars, joined, &settings->car);
		else if (mh_ascii_equal(key, "TrackName"))
			find_name(&reader->catalog->tracks, joined,
				&settings->track);
		else {
			status = select_horn(reader, joined, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	} else if (mh_ascii_equal(key, "Renderer")) {
		join_tokens(tokens, count, joined, sizeof(joined));

		for (i = 0; i < reader->catalog->renderers.count; i++)
			if (mh_ascii_equal(joined,
				reader->catalog->renderers.names[i]))
				settings->renderer = i;
	} else if (mh_ascii_equal(key, "GameType"))
		mh_cfg_choice(value, modes, sizeof(modes) / sizeof(modes[0]),
			&settings->game_type);
	else if (mh_ascii_equal(key, "Difficulty"))
		mh_cfg_choice(value, difficulty,
			sizeof(difficulty) / sizeof(difficulty[0]),
			&settings->difficulty);
	else if (mh_ascii_equal(key, "TrackLaps")) {
		/* CFG lap counts use unpadded decimal digits. */
		if (value[0] != '0' &&
			mh_arguments_unsigned(
				value, MH_RACE_MAX_LAPS, &whole) &&
			whole >= MH_RACE_MIN_LAPS)
			settings->laps = (unsigned int)whole;
	} else if (mh_ascii_equal(key, "CatchUp"))
		mh_cfg_choice(value, toggle, sizeof(toggle) / sizeof(toggle[0]),
			&settings->catch_up);
	else if (mh_ascii_equal(key, "RecordRace"))
		mh_cfg_choice(value, toggle, sizeof(toggle) / sizeof(toggle[0]),
			&settings->record_race);
	else if (mh_ascii_equal(key, "LeagueName"))
		join_tokens(tokens, count, settings->league_name,
			sizeof(settings->league_name));
	else if (mh_ascii_equal(key, "DemoFileName"))
		join_tokens(tokens, count, settings->demo_filename,
			sizeof(settings->demo_filename));
	else if (mh_ascii_equal(key, "SessionName"))
		join_tokens(tokens, count, settings->session_name,
			sizeof(settings->session_name));
	else if (mh_ascii_equal(key, "SessionPassword"))
		join_tokens(tokens, count, settings->session_password,
			sizeof(settings->session_password));
	else if (mh_ascii_equal(key, "HiscorePath")) {
		join_tokens(tokens, count, joined, sizeof(joined));
		length = strlen(joined);

		if (joined[length - 1] != '\\')
			joined[length++] = '\\';

		if (length >= sizeof(settings->hiscore_path))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"hiscore path too long");

		joined[length] = 0;
		strcpy(settings->hiscore_path, joined);
	} else if (mh_ascii_equal(key, "SerialPort") ||
		mh_ascii_equal(key, "BaudRate") ||
		mh_ascii_equal(key, "MenuCDTrack") ||
		mh_ascii_equal(key, "RateTCP") ||
		mh_ascii_equal(key, "PredictionMin") ||
		mh_ascii_equal(key, "PredictionMax")) {
		if (!mh_cfg_number(value, &numeric) ||
			!mh_cfg_integer(numeric, &whole))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid settings integer");

		if (mh_ascii_equal(key, "SerialPort")) {
			if (whole < MH_CFG_SERIAL_PORT_MIN)
				settings->serial_port = MH_CFG_SERIAL_PORT_MIN;
			else if (whole > MH_CFG_SERIAL_PORT_MAX)
				settings->serial_port = MH_CFG_SERIAL_PORT_MAX;
			else
				settings->serial_port = (unsigned int)whole;
		} else if (mh_ascii_equal(key, "RateTCP"))
			settings->tcp_rate = whole < MH_SETTINGS_RATE_MIN
				? MH_SETTINGS_RATE_MIN
				: whole > MH_SETTINGS_RATE_MAX
				? MH_SETTINGS_RATE_MAX
				: (unsigned int)whole;
		else if (mh_ascii_equal(key, "PredictionMin"))
			settings->prediction.minimum =
				whole > MH_NET_PREDICTION_LIMIT
				? MH_NET_PREDICTION_LIMIT
				: (unsigned int)whole;
		else if (mh_ascii_equal(key, "PredictionMax"))
			settings->prediction.maximum =
				whole > MH_NET_PREDICTION_LIMIT
				? MH_NET_PREDICTION_LIMIT
				: (unsigned int)whole;
		else if (mh_ascii_equal(key, "MenuCDTrack"))
			settings->menu_cd_track = whole;
		else {
			settings->baud_rate = 2;

			for (i = 0; i < sizeof(baud) / sizeof(baud[0]); i++)
				if (whole == baud[i]) {
					settings->baud_rate = i;
					break;
				}
		}
	} else if (mh_ascii_equal(key, "Protocol"))
		mh_cfg_choice(value, protocol,
			sizeof(protocol) / sizeof(protocol[0]),
			&settings->protocol);
	else if (mh_ascii_equal(key, "Parity"))
		mh_cfg_choice(value, parity, sizeof(parity) / sizeof(parity[0]),
			&settings->parity);
	else if (mh_ascii_equal(key, "StopBits"))
		mh_cfg_choice(value, stops, sizeof(stops) / sizeof(stops[0]),
			&settings->stop_bits);
	else if (mh_ascii_equal(key, "FlowControl"))
		mh_cfg_choice(value, flow, sizeof(flow) / sizeof(flow[0]),
			&settings->flow_control);
	else if (mh_ascii_equal(key, "MenuConfirmation"))
		mh_cfg_choice(value, confirmation,
			sizeof(confirmation) / sizeof(confirmation[0]),
			&settings->menu_confirmation);
	else if (length == SHORT_KEY_PREFIX_BYTES + 1 &&
		key[SHORT_KEY_PREFIX_BYTES] >= '0' &&
		key[SHORT_KEY_PREFIX_BYTES] <= '9') {
		memcpy(key_prefix, key, SHORT_KEY_PREFIX_BYTES);
		key_prefix[SHORT_KEY_PREFIX_BYTES] = 0;

		if (mh_ascii_equal(key_prefix, "ShortKey")) {
			i = (unsigned int)(key[SHORT_KEY_PREFIX_BYTES] - '0');
			join_tokens(tokens, count, settings->short_keys[i],
				MH_SETTINGS_SHORTCUT_BYTES);
		}
	} else
		return mh_cfg_av_line(tokens, count, &settings->av, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_cfg_catalog_valid(const struct mh_settings_catalog *catalog,
	const struct mh_settings *settings)
{
	unsigned int i;

	if (catalog == NULL || settings == NULL ||
		settings->course_music_count > MH_SETTINGS_COURSES ||
		!names_valid(&catalog->cars, MH_SETTINGS_CATALOG_NAME_BYTES - 1,
			0) ||
		!names_valid(&catalog->tracks,
			MH_SETTINGS_CATALOG_NAME_BYTES - 1, 0) ||
		!names_valid(
			&catalog->horns, MH_SETTINGS_HORN_NAME_BYTES - 1, 0) ||
		!names_valid(&catalog->renderers,
			MH_SETTINGS_CATALOG_NAME_BYTES - 1, 1) ||
		settings->car >= catalog->cars.count ||
		settings->track >= catalog->tracks.count ||
		settings->horn >= catalog->horns.count ||
		(settings->renderer != MH_SETTINGS_NO_RENDERER &&
			settings->renderer >= catalog->renderers.count))
		return 0;

	for (i = 0; i < settings->course_music_count; i++)
		if (settings->course_music[i].division > MH_U32_MAX ||
			settings->course_music[i].track > MH_U32_MAX)
			return 0;

	return 1;
}

enum mh_error_code
mh_settings_parse(const unsigned char *text, size_t size,
	const struct mh_settings_catalog *catalog, unsigned long *random,
	struct mh_settings *settings, struct mh_error *error_out)
{
	struct config_reader reader;
	enum mh_error_code status;

	if (random == NULL || *random > MH_U32_MAX ||
		!mh_cfg_catalog_valid(catalog, settings))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid settings catalog or state");

	reader.settings = *settings;
	reader.catalog = catalog;
	reader.random = *random;
	status = mh_cfg_parse(text, size, parse_line, &reader, error_out);

	if (status == MH_ERROR_NONE) {
		*settings = reader.settings;
		*random = reader.random;
	}

	return status;
}
