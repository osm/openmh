#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_menus.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/core/text.h"
#include "engine/game/league_document.h"
#include "engine/game/league_names.h"

#define SAVE_BUFFER_SIZE 16384
#define TEXT_LIMIT 65536
#define LINE_SIZE 1024
#define PLAYER_NAME_SIZE 16
#define CAR_FIELD_NAME_SIZE 16

enum header_field {
	HEADER_NAME = 1,
	HEADER_CREATED = 2,
	HEADER_RACE = 4,
	HEADER_RACES_DONE = 8,
	HEADER_LAPS_DONE = 16,
	HEADER_ALL = 31
};

enum driver_field {
	DRIVER_HORN = MH_PROFILE_HORN_FIELD,
	DRIVER_COLOR = 32,
	DRIVER_POINTS = 64,
	DRIVER_WINS = 128,
	DRIVER_SECOND_PLACES = 256,
	DRIVER_THIRD_PLACES = 512,
	DRIVER_ALL = 1023
};

static int
valid_identifier(const char *text, size_t capacity)
{
	size_t i;

	for (i = 0; i < capacity; i++) {
		if (text[i] == 0)
			return i != 0 && text[0] != ' ' && text[i - 1] != ' ';

		if ((unsigned char)text[i] < 32 ||
			(text[i] == '/' && i != 0 && text[i - 1] == '/'))
			return 0;
	}

	return 0;
}

/*
 * Personal's 15-byte editor permits empty names, spaces and comment bytes.
 * The original constructor/writer copy these literally; only reload applies
 * the lexer. Keep asset identifiers under the separate valid_identifier()
 * constraints.
 */
static int
valid_player_name(const char *text)
{
	size_t i;

	for (i = 0; i < PLAYER_NAME_SIZE; i++) {
		if (text[i] == 0)
			return 1;

		if ((unsigned char)text[i] < 32)
			return 0;
	}

	return 0;
}

/*
 * Join HumanPlayer tokens, truncate to 15 bytes and copy the result (even
 * empty) into both full name and nickname.
 */
static void
normalize_player_name(char *text)
{
	size_t read, written;
	int space;

	read = written = 0;
	space = 0;

	while (text[read] != 0 && written < PLAYER_NAME_SIZE - 1) {
		if (text[read] == ' ' || text[read] == '\t') {
			if (written != 0)
				space = 1;
		} else {
			if (space)
				text[written++] = ' ';

			if (written == PLAYER_NAME_SIZE - 1)
				break;

			text[written++] = text[read];
			space = 0;
		}

		read++;
	}

	text[written] = 0;
}

/*
 * Avoid implementation-defined unsigned-to-signed conversion on 32-bit hosts.
 */
static long
signed_u32(unsigned long value)
{
	return value <= (unsigned long)MH_I32_MAX
		? (long)value
		: -1L - (long)(MH_U32_MAX - value);
}

enum mh_error_code
mh_league_save_text(const struct mh_league *league,
	const struct mh_profile *profiles, unsigned int profile_count,
	const char *name, unsigned long created, unsigned long races,
	unsigned long laps, unsigned char **text, size_t *size,
	struct mh_error *error_out)
{
	const struct mh_league_entry *entry;
	const struct mh_profile *profile;
	char *buffer, *cursor;
	unsigned int i, j, division;
	unsigned long color;

	if (league == NULL || profiles == NULL || name == NULL ||
		text == NULL || size == NULL ||
		!valid_identifier(name, MH_LEAGUE_NAME_SIZE) ||
		created > MH_U32_MAX || races > MH_U32_MAX ||
		laps > MH_U32_MAX || league->race_index > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league save input");

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		entry = &league->entries[i];

		if (entry->driver >= profile_count ||
			entry->human > MH_U32_MAX ||
			entry->points > MH_U32_MAX ||
			entry->wins > MH_U32_MAX ||
			entry->second_places > MH_U32_MAX ||
			entry->third_places > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league save entry");

		profile = &profiles[entry->driver];

		if (!(entry->human ? valid_player_name(profile->nick)
				: valid_identifier(profile->nick,
					sizeof(profile->nick))) ||
			!valid_identifier(
				profile->horn, sizeof(profile->horn)) ||
			profile->colors[0] > MH_PROFILE_RGB_MASK ||
			(profile->league_fields & MH_PROFILE_LEAGUE_FIELDS) !=
				MH_PROFILE_LEAGUE_FIELDS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"incomplete league driver metadata");

		for (j = 0; j < MH_LEAGUE_GROUPS; j++)
			if (!valid_identifier(
				profile->cars[j], sizeof(profile->cars[j])))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid league car name");
	}

	/*
	 * Each validated entry emits fewer than 512 bytes, plus <256 header.
	 * Fixed 16 KiB allocation bounds every sprintf without C99 snprintf.
	 */
	buffer = (char *)malloc(SAVE_BUFFER_SIZE);

	if (buffer == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory saving league");

	cursor = buffer;
	cursor += sprintf(cursor,
		"LeagueName\t%s\nCreateDate\t%ld\nRaceNumber\t%ld\nRacesDone\t%ld\nLapsDone\t%ld\n",
		name, signed_u32(created), signed_u32(league->race_index),
		signed_u32(races), signed_u32(laps));
	division = 0;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		if (i == 0 || i == 2 || i == 10 || i == 18)
			cursor += sprintf(
				cursor, "\n// Division %u\n", division++);

		entry = &league->entries[i];
		profile = &profiles[entry->driver];
		cursor += sprintf(cursor, "%s\t%s\n",
			entry->human ? "HumanPlayer" : "AIPlayer",
			profile->nick);

		for (j = 0; j < MH_LEAGUE_GROUPS; j++)
			cursor += sprintf(cursor, "CarDivision%u\t%s\n", j,
				profile->cars[j]);

		color = profile->colors[0];
		cursor += sprintf(cursor,
			"Colour\t%lu %lu %lu\nCarHorn\t%s\nScore\t%ld\nWins\t%ld\nSeconds\t%ld\nThirds\t%ld\n",
			(color >> 16) & 255UL, (color >> 8) & 255UL,
			color & 255UL, profile->horn, signed_u32(entry->points),
			signed_u32(entry->wins),
			signed_u32(entry->second_places),
			signed_u32(entry->third_places));
	}

	*text = (unsigned char *)buffer;
	*size = (size_t)(cursor - buffer);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
parse_u32(const char *text, unsigned long *output)
{
	unsigned long value, digit;
	int negative;

	negative = *text == '-';

	if (negative)
		text++;

	if (*text == 0)
		return 0;

	value = 0;

	while (*text != 0) {
		if (*text < '0' || *text > '9')
			return 0;

		digit = (unsigned long)(*text++ - '0');

		if (value > (MH_U32_MAX - digit) / 10)
			return 0;

		value = value * 10 + digit;
	}

	if (negative && value > MH_U32_SIGN_BIT)
		return 0;

	*output = negative ? (0UL - value) & MH_U32_MAX : value;

	return 1;
}

/* Decode one bounded line and split its key/value after original comments. */
static enum mh_error_code
read_line(const unsigned char *text, size_t size, size_t *offset,
	char line[LINE_SIZE], char **key, char **value,
	struct mh_error *error_out)
{
	size_t cursor, length;
	char *token, *comment;

	cursor = *offset;
	length = 0;

	while (cursor < size && text[cursor] != '\n' && text[cursor] != '\r') {
		if ((text[cursor] < 32 && text[cursor] != '\t') ||
			length == LINE_SIZE - 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league text line");

		line[length++] = (char)text[cursor++];
	}

	if (cursor < size && text[cursor] == '\r')
		cursor++;

	if (cursor < size && text[cursor] == '\n')
		cursor++;

	line[length] = 0;
	comment = strstr(line, "//");

	if (comment != NULL)
		*comment = 0;

	comment = strchr(line, ';');

	if (comment != NULL)
		*comment = 0;

	length = strlen(line);

	while (length != 0 &&
		(line[length - 1] == ' ' || line[length - 1] == '\t'))
		line[--length] = 0;

	token = line;

	while (*token == ' ' || *token == '\t')
		token++;

	if (*token == 0) {
		*offset = cursor;
		*key = *value = token;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	*key = token;

	while (*token && *token != ' ' && *token != '\t')
		token++;

	if (*token)
		*token++ = 0;

	while (*token == ' ' || *token == '\t')
		token++;

	*offset = cursor;
	*value = token;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
select_header_field(struct mh_league_document *document, const char *key,
	unsigned long **number)
{
	unsigned int field_bit;

	field_bit = 0;
	*number = NULL;

	if (mh_ascii_equal(key, "LeagueName"))
		field_bit = HEADER_NAME;
	else if (mh_ascii_equal(key, "CreateDate")) {
		field_bit = HEADER_CREATED;
		*number = &document->created;
	} else if (mh_ascii_equal(key, "RaceNumber")) {
		field_bit = HEADER_RACE;
		*number = &document->league.race_index;
	} else if (mh_ascii_equal(key, "RacesDone")) {
		field_bit = HEADER_RACES_DONE;
		*number = &document->races;
	} else if (mh_ascii_equal(key, "LapsDone")) {
		field_bit = HEADER_LAPS_DONE;
		*number = &document->laps;
	}

	return field_bit;
}

static enum mh_error_code
load_driver_profile(const struct mh_profile *ai, unsigned int ai_count,
	char *name, int human, struct mh_profile *profile,
	struct mh_error *error_out)
{
	unsigned int i;

	if (human)
		normalize_player_name(name);
	else if (!valid_identifier(name, MH_LEAGUE_NAME_SIZE))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league driver name");

	if (!human) {
		for (i = 0; i < ai_count; i++) {
			if (!valid_identifier(ai[i].nick, sizeof(ai[i].nick)))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid AI profile table");

			if (mh_ascii_equal(name, ai[i].nick))
				break;
		}

		if (i == ai_count)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"unresolved league AI driver");

		*profile = ai[i];
	} else {
		strcpy(profile->name, name);
		strcpy(profile->nick, name);
		strcpy(profile->picture, MH_ASSET_PLAYER_PICTURE);
		profile->aggressiveness = profile->eagerness = 0.5;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
parse_color(char *text, unsigned long *output, struct mh_error *error_out)
{
	char *part;
	unsigned int component;
	unsigned long color, value;

	color = 0;

	for (component = 0; component < 3; component++) {
		part = text;

		while (*text && *text != ' ' && *text != '\t')
			text++;

		if (*text)
			*text++ = 0;

		if (!parse_u32(part, &value) || value > 255)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid saved color");

		color = (color << 8) | value;

		while (*text == ' ' || *text == '\t')
			text++;
	}

	if (*text)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"excess saved color components");

	*output = color;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_load_text(const unsigned char *text, size_t size,
	const struct mh_profile *ai, unsigned int ai_count,
	struct mh_league_document *output, struct mh_error *error_out)
{
	struct mh_league_document document;
	struct mh_profile *profile;
	struct mh_league_entry *entry;
	char line[LINE_SIZE], *value_text = NULL, *key = NULL;
	enum mh_error_code status;
	size_t offset;
	unsigned int entry_count, header_fields, driver_fields, field_bit, i;
	unsigned long *number;
	int human;

	if (text == NULL || output == NULL || (ai_count != 0 && ai == NULL) ||
		size > TEXT_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league text input");

	memset(&document, 0, sizeof(document));
	offset = 0;
	entry_count = 0;
	header_fields = 0;
	driver_fields = 0;

	while (offset < size) {
		status = read_line(text, size, &offset, line, &key, &value_text,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (*key == 0)
			continue;

		field_bit = select_header_field(&document, key, &number);

		if (field_bit) {
			if (header_fields & field_bit)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate league header");

			if (number != NULL) {
				if (!parse_u32(value_text, number))
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"invalid league counter");
			} else {
				if (!valid_identifier(
					value_text, MH_LEAGUE_NAME_SIZE))
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"invalid league name");

				strcpy(document.name, value_text);
			}

			header_fields |= field_bit;
			continue;
		}

		human = mh_ascii_equal(key, "HumanPlayer");

		if (human || mh_ascii_equal(key, "AIPlayer")) {
			if (entry_count == MH_LEAGUE_ENTRIES ||
				(entry_count != 0 &&
					driver_fields != DRIVER_ALL))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid league roster");

			profile = &document.profiles[entry_count];
			entry = &document.league.entries[entry_count];

			status = load_driver_profile(ai, ai_count, value_text,
				human, profile, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			entry->driver = entry_count;
			entry->human = human;
			entry_count++;
			driver_fields = 0;
			continue;
		}

		if (entry_count == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"league field before driver");

		profile = &document.profiles[entry_count - 1];
		entry = &document.league.entries[entry_count - 1];

		for (i = 0; i < MH_LEAGUE_GROUPS; i++) {
			char car_key[CAR_FIELD_NAME_SIZE];

			sprintf(car_key, "CarDivision%u", i);

			if (mh_ascii_equal(key, car_key))
				break;
		}

		if (i < MH_LEAGUE_GROUPS) {
			field_bit = 1U << i;

			if (!valid_identifier(
				value_text, sizeof(profile->cars[i])))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid saved car name");

			strcpy(profile->cars[i], value_text);
			profile->league_fields |= field_bit;
		} else if (mh_ascii_equal(key, "CarHorn")) {
			field_bit = DRIVER_HORN;

			if (!valid_identifier(
				value_text, sizeof(profile->horn)))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid saved horn name");

			strcpy(profile->horn, value_text);
			profile->league_fields |= field_bit;
		} else if (mh_ascii_equal(key, "Colour")) {
			field_bit = DRIVER_COLOR;
			status = parse_color(
				value_text, &profile->colors[0], error_out);

			if (status != MH_ERROR_NONE)
				return status;

			profile->color_fields |= 1;
		} else {
			if (mh_ascii_equal(key, "Score")) {
				field_bit = DRIVER_POINTS;
				number = &entry->points;
			} else if (mh_ascii_equal(key, "Wins")) {
				field_bit = DRIVER_WINS;
				number = &entry->wins;
			} else if (mh_ascii_equal(key, "Seconds")) {
				field_bit = DRIVER_SECOND_PLACES;
				number = &entry->second_places;
			} else if (mh_ascii_equal(key, "Thirds")) {
				field_bit = DRIVER_THIRD_PLACES;
				number = &entry->third_places;
			} else
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"unknown league field");

			if (!parse_u32(value_text, number))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid saved driver counter");
		}

		if (driver_fields & field_bit)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate league driver field");

		driver_fields |= field_bit;
	}

	if (header_fields != HEADER_ALL || entry_count != MH_LEAGUE_ENTRIES ||
		driver_fields != DRIVER_ALL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "incomplete league file");

	*output = document;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_create(const struct mh_profile *ai, unsigned int ai_count,
	const char *league_name, const char *human_name, const char *car,
	unsigned long color, unsigned long created, unsigned long *random,
	struct mh_league_document *output, struct mh_error *error_out)
{
	struct mh_league_document next;
	unsigned int selected[MH_LEAGUE_ENTRIES], i, j, candidate, human;
	unsigned long seed;
	char generated[MH_LEAGUE_NAME_SIZE];
	enum mh_error_code status;

	if (ai == NULL || ai_count < MH_LEAGUE_ENTRIES ||
		ai_count > MH_RANDOM_RESULT_COUNT || human_name == NULL ||
		car == NULL || random == NULL || output == NULL ||
		*random > MH_U32_MAX || created > MH_U32_MAX ||
		color > MH_U32_MAX ||
		(league_name != NULL &&
			!valid_identifier(league_name, MH_LEAGUE_NAME_SIZE)) ||
		!valid_player_name(human_name) ||
		!valid_identifier(car, sizeof(next.profiles[0].cars[0])))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid new league input");

	for (i = 0; i < ai_count; i++) {
		if (!valid_identifier(ai[i].nick, sizeof(ai[i].nick)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league AI name");

		for (j = 0; j < i; j++)
			if (mh_ascii_equal(ai[i].nick, ai[j].nick))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ambiguous league AI name");
	}

	memset(&next, 0, sizeof(next));
	seed = *random;

	if (league_name == NULL) {
		status = mh_league_auto_name(&seed, generated, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		league_name = generated;
	}

	strcpy(next.name, league_name);
	next.created = created;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		do {
			candidate = mh_random_next(&seed) % ai_count;

			for (j = 0; j < i && selected[j] != candidate; j++) {
			}
		} while (j != i);

		selected[i] = candidate;
		next.profiles[i] = ai[candidate];
		next.league.entries[i].driver = i;
	}

	human = MH_LEAGUE_ENTRIES - MH_LEAGUE_DIVISION_SIZE +
		mh_random_next(&seed) % MH_LEAGUE_DIVISION_SIZE;
	next.league.entries[human].human = 1;
	strcpy(next.profiles[human].name, human_name);
	strcpy(next.profiles[human].nick, human_name);

	for (i = 0; i < MH_LEAGUE_GROUPS; i++)
		strcpy(next.profiles[human].cars[i], car);

	next.profiles[human].colors[0] = color;
	next.profiles[human].color_fields |= 1;
	next.profiles[human].league_fields |= MH_PROFILE_CAR_FIELDS;
	*output = next;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
