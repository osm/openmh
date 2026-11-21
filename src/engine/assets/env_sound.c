#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/env_sound.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"

#define TEXT_BYTES_LIMIT 65536
#define LINE_BYTES 512
#define SPACE_LIMIT 1000000
#define COLLISION_CLASS_MAX 15
#define PRIORITY_MAX 255
#define VOLUME_MAX 100
#define FIXED_PITCH_MIN .01
#define PITCH_MAX 16

static char *
next_token(char **cursor)
{
	char *start;

	while (**cursor == ' ' || **cursor == '\t')
		(*cursor)++;

	if (**cursor == 0)
		return NULL;

	start = *cursor;

	while (**cursor != 0 && **cursor != ' ' && **cursor != '\t')
		(*cursor)++;

	if (**cursor != 0)
		*(*cursor)++ = 0;

	return start;
}

static int
read_float(char **cursor, double minimum, double maximum, double *output)
{
	char *value_text, *end;
	double value;

	value_text = next_token(cursor);

	if (value_text == NULL)
		return 0;

	errno = 0;
	value = strtod(value_text, &end);

	if (value_text == end || *end != 0 || errno != 0 ||
		!(value >= minimum && value <= maximum))
		return 0;

	*output = (double)(float)value;

	return 1;
}

static int
read_uint(char **cursor, unsigned int maximum, unsigned int *output)
{
	char *value_text, *end;
	unsigned long value;

	value_text = next_token(cursor);

	if (value_text == NULL || *value_text < '0' || *value_text > '9')
		return 0;

	errno = 0;
	value = strtoul(value_text, &end, 10);

	if (*end != 0 || errno != 0 || value > maximum)
		return 0;

	*output = (unsigned int)value;

	return 1;
}

static enum mh_error_code
read_record(const char *kind, char **cursor, struct mh_env_sound *sound,
	struct mh_error *error_out)
{
	char *value_text;
	unsigned int axis;

	if (mh_ascii_equal(kind, "envsound"))
		sound->kind = MH_ENV_FIXED;
	else if (mh_ascii_equal(kind, "objectenvsound"))
		sound->kind = MH_ENV_OBJECT;
	else if (mh_ascii_equal(kind, "objectcolsound"))
		sound->kind = MH_ENV_COLLISION;
	else
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unknown ESD record");

	if (sound->kind != MH_ENV_FIXED &&
		!read_uint(cursor,
			sound->kind == MH_ENV_COLLISION ? COLLISION_CLASS_MAX
							: MH_U16_MAX,
			&sound->object_class))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ESD object class");

	value_text = next_token(cursor);

	if (value_text == NULL || strlen(value_text) >= sizeof(sound->sample) ||
		value_text[0] == '.' || strchr(value_text, '/') != NULL ||
		strchr(value_text, '\\') != NULL ||
		strchr(value_text, ':') != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ESD sample basename");

	strcpy(sound->sample, value_text);

	if (!read_uint(cursor, PRIORITY_MAX, &sound->priority) ||
		!read_float(cursor, 0, VOLUME_MAX, &sound->volume) ||
		!read_float(cursor,
			sound->kind == MH_ENV_FIXED ? FIXED_PITCH_MIN : 0,
			PITCH_MAX, &sound->pitch))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ESD sound parameters");

	if (sound->kind != MH_ENV_COLLISION) {
		if (!read_float(
			cursor, 0, SPACE_LIMIT, &sound->near_distance) ||
			!read_float(
				cursor, 0, SPACE_LIMIT, &sound->far_distance) ||
			sound->near_distance >= sound->far_distance)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid ESD sound range");
	}

	if (sound->kind == MH_ENV_FIXED)
		for (axis = 0; axis < 3; axis++)
			if (!read_float(cursor, -SPACE_LIMIT, SPACE_LIMIT,
				&sound->position[axis]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid ESD position");

	if (next_token(cursor) != NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "extra ESD values");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_env_sounds_esd_decode(const unsigned char *text, size_t size,
	struct mh_env_sounds *output, struct mh_error *error_out)
{
	struct mh_env_sounds parsed;
	struct mh_env_sound *sound;
	char line[LINE_BYTES], *cursor, *value_text, *comment;
	size_t offset, length;
	enum mh_error_code status;

	if (text == NULL || output == NULL || size > TEXT_BYTES_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid ESD input");

	memset(&parsed, 0, sizeof(parsed));
	offset = 0;

	while (offset < size) {
		length = 0;

		while (offset < size && text[offset] != '\r' &&
			text[offset] != '\n') {
			if ((text[offset] < 32 && text[offset] != '\t') ||
				text[offset] > 126)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"non-ASCII ESD text");

			if (length == sizeof(line) - 1)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"ESD line too long");

			line[length++] = (char)text[offset++];
		}

		if (offset < size && text[offset] == '\r')
			offset++;

		if (offset < size && text[offset] == '\n')
			offset++;

		line[length] = 0;
		comment = strstr(line, "//");

		if (comment != NULL)
			*comment = 0;

		cursor = line;
		value_text = next_token(&cursor);

		if (value_text == NULL)
			continue;

		if (parsed.count == MH_ENV_SOUNDS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many ESD records");

		sound = &parsed.sounds[parsed.count];

		status = read_record(value_text, &cursor, sound, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		parsed.count++;
	}

	*output = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
