#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/horn.h"
#include "engine/core/error.h"
#include "engine/core/text.h"

#define TEXT_BYTES_LIMIT 65536
#define LINE_BYTES 512
#define VOLUME_PERCENT_MIN (-100)
#define VOLUME_PERCENT_MAX 700
#define PITCH_PERCENT_MIN (-90)
#define PITCH_PERCENT_MAX 300

static int
valid_sample_path(const char *path)
{
	const char *component;
	size_t length;

	if (*path == 0 || *path == '/' || *path == '\\' ||
		strchr(path, ':') != NULL)
		return 0;

	do {
		component = path;

		while (*path != 0 && *path != '/' && *path != '\\')
			path++;

		length = (size_t)(path - component);

		if (length == 0 || (length == 1 && component[0] == '.') ||
			(length == 2 && component[0] == '.' &&
				component[1] == '.'))
			return 0;

		if (*path == 0)
			return 1;

		path++;
	} while (*path != 0);

	return 0;
}

static int
parse_factor(const char *text, double minimum, double maximum, double *output)
{
	char *end;
	double value;

	errno = 0;
	value = strtod(text, &end);

	if (text == end || *end != 0 || errno != 0 ||
		!(value >= minimum && value <= maximum))
		return 0;

	*output = (double)(float)(1 + (long double)value * .01);

	return 1;
}

/* Keep the key in line and collapse value whitespace to single spaces. */
static size_t
split_property(char *line, char **key, char value[LINE_BYTES])
{
	char *cursor;
	size_t value_length;

	cursor = line;

	while (*cursor == ' ' || *cursor == '\t')
		cursor++;

	*key = cursor;

	while (*cursor != 0 && *cursor != ' ' && *cursor != '\t')
		cursor++;

	if (*cursor != 0)
		*cursor++ = 0;

	value_length = 0;

	while (*cursor != 0) {
		while (*cursor == ' ' || *cursor == '\t')
			cursor++;

		if (*cursor == 0)
			break;

		if (value_length != 0)
			value[value_length++] = ' ';

		while (*cursor != 0 && *cursor != ' ' && *cursor != '\t')
			value[value_length++] = *cursor++;
	}

	value[value_length] = 0;

	return value_length;
}

enum mh_error_code
mh_horn_chf_decode(const unsigned char *text, size_t size, unsigned int index,
	struct mh_horn *output, struct mh_error *error_out)
{
	struct mh_horn horn;
	char line[LINE_BYTES], value[LINE_BYTES], *key, *comment;
	size_t offset, length, value_length;

	if (text == NULL || output == NULL || size > TEXT_BYTES_LIMIT ||
		index >= MH_HORNS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid CHF input");

	memset(&horn, 0, sizeof(horn));
	sprintf(horn.name, "Horn %02u", index);
	strcpy(horn.sample, "<None>");
	horn.volume = horn.pitch = 1;
	offset = 0;

	while (offset < size) {
		length = 0;

		while (offset < size && text[offset] != '\r' &&
			text[offset] != '\n') {
			if ((text[offset] < 32 && text[offset] != '\t') ||
				text[offset] > 126)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"non-ASCII CHF text");

			if (length == sizeof(line) - 1)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"CHF line too long");

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

		value_length = split_property(line, &key, value);

		if (*key == 0)
			continue;

		if (mh_ascii_equal(key, "Name")) {
			if (value_length == 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"empty CHF name");

			if (value_length >= sizeof(horn.name))
				value_length = sizeof(horn.name) - 1;

			memset(horn.name, 0, sizeof(horn.name));
			memcpy(horn.name, value, value_length);
		} else if (mh_ascii_equal(key, "SampleName")) {
			/*
			 * The original truncates at 127; refuse ambiguous
			 * truncated paths.
			 */
			if (value_length >= sizeof(horn.sample) ||
				!valid_sample_path(value))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid CHF sample path");

			strcpy(horn.sample, value);
		} else if (mh_ascii_equal(key, "VolumeChange")) {
			if (!parse_factor(value, VOLUME_PERCENT_MIN,
				VOLUME_PERCENT_MAX, &horn.volume))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid CHF volume percentage");
		} else if (mh_ascii_equal(key, "PitchChange")) {
			if (!parse_factor(value, PITCH_PERCENT_MIN,
				PITCH_PERCENT_MAX, &horn.pitch))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid CHF pitch percentage");
		}
	}

	*output = horn;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
