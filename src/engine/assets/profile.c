#include <errno.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/config_line.h"
#include "engine/assets/profile.h"
#include "engine/core/error.h"

#define PROFILE_LINE_BYTES 1024
#define DEFAULT_PLAYER_COLOR 0xffcc88UL
#define DEFAULT_CAR_COLOR 0xffff00UL

enum {
	FIELD_NAME = 1,
	FIELD_NICK = 2,
	FIELD_PICTURE = 4,
	FIELD_FIRST_CAR = 8,
	FIELD_HORN = 128,
	PRESENTATION_FIELDS = FIELD_NAME | FIELD_NICK | FIELD_PICTURE,
	LEAGUE_FIELD_SHIFT = 3,
	TUNING_AGGRESSIVENESS = 1,
	TUNING_EAGERNESS = 2
};

static int
read_color(const char *text, unsigned long *output)
{
	unsigned long value, packed;
	unsigned int component;
	char *end;

	packed = 0;
	component = 0;

	while (*text != 0) {
		if (component == 3 || *text < '0' || *text > '9')
			return 0;

		errno = 0;
		value = strtoul(text, &end, 10);

		if (errno != 0 || value > 255 ||
			(*end != 0 && *end != ' ' && *end != '\t'))
			return 0;

		packed |= value << (16 - component * 8);
		component++;
		text = end;

		while (*text == ' ' || *text == '\t')
			text++;
	}

	if (component == 0)
		return 0;

	*output = packed;

	return 1;
}

static enum mh_error_code
read_line(const unsigned char *text, size_t size, size_t *position,
	char line[PROFILE_LINE_BYTES], struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_config_line_read(
		text, size, position, line, PROFILE_LINE_BYTES, 255, 1);

	if (status == MH_ERROR_UNSUPPORTED)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"profile contains unsupported control bytes");

	if (status == MH_ERROR_RANGE)
		return mh_error_set(error_out, status, "profile line too long");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
valid_picture_name(const char *name)
{
	const char *p;

	for (p = name; *p != 0; p++) {
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			(*p >= '0' && *p <= '9') || *p == '.' ||
			*p == '_' || *p == '-'))
			return 0;
	}

	return *name != '.' && strstr(name, "..") == NULL;
}

static enum mh_error_code
read_tuning(const char *text, double *output, struct mh_error *error_out)
{
	char *end;
	double value;

	errno = 0;
	value = strtod(text, &end);

	if (text == end || *end != 0 || errno != 0 ||
		!(value >= -DBL_MAX && value <= DBL_MAX))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid profile tuning");

	if (value < 0)
		value = 0;
	else if (value > 100)
		value = 1;
	else
		value = (double)(float)((long double)value * (double)0.01);

	*output = value;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_profile_adp_decode(const unsigned char *text, size_t size,
	struct mh_profile *output, struct mh_error *error_out)
{
	struct mh_profile profile;
	char line[PROFILE_LINE_BYTES], *value_text, *key, *destination;
	size_t position, capacity;
	enum mh_error_code status;
	unsigned int fields, field_flag, i;
	double value;

	if (text == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid profile input");

	memset(&profile, 0, sizeof(profile));

	/* ADP profile defaults. */
	profile.aggressiveness = profile.eagerness = 0.5;
	profile.colors[0] = DEFAULT_PLAYER_COLOR;

	for (i = 1; i < 4; i++)
		profile.colors[i] = DEFAULT_CAR_COLOR;

	position = 0;
	fields = 0;

	while (position < size) {
		status = read_line(text, size, &position, line, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		value_text = line;

		while (*value_text == ' ' || *value_text == '\t')
			value_text++;

		key = value_text;

		while (*value_text != 0 && *value_text != ' ' &&
			*value_text != '\t') {
			if (*value_text >= 'A' && *value_text <= 'Z')
				*value_text += 'a' - 'A';

			value_text++;
		}

		if (*value_text != 0)
			*value_text++ = 0;

		while (*value_text == ' ' || *value_text == '\t')
			value_text++;

		i = 4;

		if (strcmp(key, "playercolour") == 0)
			i = 0;
		else if (strlen(key) == 10 &&
			strncmp(key, "carcolour", 9) == 0 && key[9] >= '1' &&
			key[9] <= '3')
			i = (unsigned int)(key[9] - '0');

		if (i < 4) {
			field_flag = 1U << i;

			if ((profile.color_fields & field_flag) != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate profile color");

			if (!read_color(value_text, &profile.colors[i]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid profile color");

			profile.color_fields |= field_flag;
			continue;
		}

		if (strcmp(key, "aggressiveness") == 0 ||
			strcmp(key, "eagerness") == 0) {
			field_flag = strcmp(key, "aggressiveness") == 0
				? TUNING_AGGRESSIVENESS
				: TUNING_EAGERNESS;

			if ((profile.tuning_fields & field_flag) != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate profile tuning");

			value = 0;
			status = read_tuning(value_text, &value, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (field_flag == TUNING_AGGRESSIVENESS)
				profile.aggressiveness = value;
			else
				profile.eagerness = value;
			profile.tuning_fields |= field_flag;
			continue;
		}

		field_flag = 0;
		capacity = 0;
		destination = NULL;

		if (strcmp(key, "playername") == 0) {
			field_flag = FIELD_NAME;
			destination = profile.name;
			capacity = sizeof(profile.name);
		}

		if (strcmp(key, "playernick") == 0) {
			field_flag = FIELD_NICK;
			destination = profile.nick;
			capacity = sizeof(profile.nick);
		}

		if (strcmp(key, "playerpicture") == 0) {
			field_flag = FIELD_PICTURE;
			destination = profile.picture;
			capacity = sizeof(profile.picture);
		}

		for (i = 0; i < 4; i++) {
			if (strlen(key) == 12 &&
				strncmp(key, "cardivision", 11) == 0 &&
				key[11] == (char)('0' + i)) {
				field_flag = (unsigned int)FIELD_FIRST_CAR << i;
				destination = profile.cars[i];
				capacity = sizeof(profile.cars[i]);
			}
		}

		if (strcmp(key, "carhorn") == 0) {
			field_flag = FIELD_HORN;
			destination = profile.horn;
			capacity = sizeof(profile.horn);
		}

		if (field_flag == 0)
			continue;

		if ((fields & field_flag) != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate profile field");

		if (*value_text == 0 || strlen(value_text) >= capacity)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid profile field length");

		if (field_flag == FIELD_PICTURE &&
			!valid_picture_name(value_text))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid profile picture basename");

		strcpy(destination, value_text);
		fields |= field_flag;
	}

	profile.league_fields = fields >> LEAGUE_FIELD_SHIFT;

	if ((fields & PRESENTATION_FIELDS) != PRESENTATION_FIELDS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing profile presentation field");

	*output = profile;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
