#include <stdio.h>
#include <string.h>

#include "engine/archive/path.h"
#include "engine/assets/prepare_cue.h"
#include "engine/core/text.h"

static enum mh_error_code
read_filename(const char *line, char output[MH_DISC_TRACK_NAME_SIZE],
	struct mh_error *error_out)
{
	const char *end;
	char name[MH_DISC_TRACK_NAME_SIZE], checked[MH_ARCHIVE_PATH_SIZE];
	size_t length;
	enum mh_error_code status;

	if (strncmp(line, "FILE \"", 6) != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected a quoted CUE FILE declaration");

	end = strchr(line + 6, '"');

	if (end == NULL || strcmp(end, "\" BINARY") != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected a separate BINARY track file");

	length = (size_t)(end - line - 6);

	if (length >= sizeof(name))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "CUE filename is too long");

	memcpy(name, line + 6, length);
	name[length] = '\0';
	status = mh_archive_path("", name, checked, error_out);

	if (status == MH_ERROR_NONE)
		memcpy(output, name, length + 1);

	return status;
}

static enum mh_error_code
parse_line(struct mh_cue *cue, const char *line, unsigned int *record,
	struct mh_error *error_out)
{
	char expected[40];
	unsigned int track, part, i;
	enum mh_error_code status;

	track = *record / 3;
	part = *record % 3;

	if (track >= MH_RETAIL_DISC_TRACKS)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"CUE contains more than the supplied eleven tracks");

	if (part == 0) {
		status = read_filename(line, cue->files[track], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < track; i++) {
			if (mh_ascii_equal(cue->files[i], cue->files[track]))
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"CUE tracks must use separate files");
		}
	} else {
		if (part == 1)
			sprintf(expected, "TRACK %02u %s", track + 1,
				track == 0 ? "MODE1/2352" : "AUDIO");
		else
			strcpy(expected, "INDEX 01 00:00:00");

		if (strcmp(line, expected) != 0)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported CUE track layout; for cdrdao images, use the original .toc file");
	}

	(*record)++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_cue_parse(const void *data, size_t size, struct mh_cue *output,
	struct mh_error *error_out)
{
	struct mh_cue cue;
	const unsigned char *bytes;
	char line[MH_DISC_TRACK_NAME_SIZE + 32];
	size_t offset, start, end, length;
	unsigned int record;
	enum mh_error_code status;

	if (data == NULL || output == NULL || size == 0 ||
		size > MH_DISC_LAYOUT_TEXT_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid CUE text");

	bytes = data;
	memset(&cue, 0, sizeof(cue));
	record = 0;
	offset = 0;

	while (offset < size) {
		start = offset;

		while (offset < size && bytes[offset] != '\n') {
			if (bytes[offset] == 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"NUL in CUE text");

			offset++;
		}

		end = offset;

		if (offset < size)
			offset++;

		while (start < end &&
			(bytes[start] == ' ' || bytes[start] == '\t'))
			start++;

		while (end > start &&
			(bytes[end - 1] == ' ' || bytes[end - 1] == '\t' ||
				bytes[end - 1] == '\r'))
			end--;

		length = end - start;

		if (length == 0)
			continue;

		if (length >= sizeof(line))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"CUE line is too long");

		memcpy(line, bytes + start, length);
		line[length] = '\0';
		status = parse_line(&cue, line, &record, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (record != MH_RETAIL_DISC_TRACKS * 3)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected one data track and ten separate audio tracks");

	*output = cue;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
