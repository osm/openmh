#include <string.h>

#include "engine/archive/path.h"
#include "engine/assets/prepare_cdrdao.h"
#include "engine/core/limits.h"

enum {
	CD_FRAMES_PER_SECOND = 75,
	CD_PREGAP_FRAMES = 150
};

struct toc_reader {
	struct mh_disc_layout	layout;
	unsigned int		tracks;
	int			header, file, silence, start;
};

static void
skip_spaces(const char **text)
{
	while (**text == ' ' || **text == '\t')
		(*text)++;
}

static int
read_number(const char **text, unsigned long *output)
{
	unsigned long number, digit;
	const char *p;

	p = *text;
	number = 0;

	if (*p < '0' || *p > '9')
		return 0;

	while (*p >= '0' && *p <= '9') {
		digit = (unsigned long)(*p++ - '0');

		if (number > (MH_U32_MAX - digit) / 10)
			return 0;

		number = number * 10 + digit;
	}

	*text = p;
	*output = number;

	return 1;
}

static int
read_frames(const char **text, unsigned long *output)
{
	unsigned long minutes, seconds, frames;

	if (!read_number(text, &minutes))
		return 0;

	/* cdrdao writes a bare zero for the first audio file offset. */
	if (minutes == 0 && (**text == ' ' || **text == '\t' || **text == 0)) {
		*output = 0;
		return 1;
	}

	if (**text != ':')
		return 0;

	(*text)++;

	if (!read_number(text, &seconds) || seconds >= 60 || **text != ':')
		return 0;

	(*text)++;

	if (!read_number(text, &frames) || frames >= CD_FRAMES_PER_SECOND ||
		minutes > (MH_U32_MAX / CD_FRAMES_PER_SECOND - seconds) / 60)
		return 0;

	minutes = (minutes * 60 + seconds) * CD_FRAMES_PER_SECOND;

	if (frames > MH_U32_MAX - minutes)
		return 0;

	*output = minutes + frames;

	return 1;
}

static int
read_name(const char **text, char name[MH_DISC_TRACK_NAME_SIZE])
{
	const char *end;
	size_t length;
	char checked[MH_ARCHIVE_PATH_SIZE];

	if (**text != '"')
		return 0;

	(*text)++;
	end = strchr(*text, '"');

	if (end == NULL)
		return 0;

	length = (size_t)(end - *text);

	if (length == 0 || length >= MH_DISC_TRACK_NAME_SIZE)
		return 0;

	memcpy(name, *text, length);
	name[length] = 0;
	*text = end + 1;

	return mh_archive_path("", name, checked, NULL) == MH_ERROR_NONE;
}

static int
next_field(const char **text)
{
	if (**text != ' ' && **text != '\t')
		return 0;

	skip_spaces(text);

	return **text != 0;
}

static int
read_file(struct toc_reader *reader, const char *text, int audio)
{
	struct mh_disc_layout *layout;
	unsigned long base, start, frames, stride, offset, length;
	unsigned int track;
	char name[MH_DISC_TRACK_NAME_SIZE];

	layout = &reader->layout;
	track = reader->tracks - 1;
	base = 0;
	start = 0;
	stride = audio || layout->data_format == MH_DISC_MODE1
		? MH_DISC_SECTOR_BYTES
		: MH_DISC_BLOCK_BYTES;

	if (reader->file || audio != (track != 0) || !read_name(&text, name) ||
		!next_field(&text))
		return 0;

	if (audio) {
		if (*text++ != '#' || !read_number(&text, &base) ||
			!next_field(&text) || !read_frames(&text, &start) ||
			!next_field(&text) ||
			strcmp(name, layout->tracks[0].file) != 0)
			return 0;
	}

	if (!read_frames(&text, &frames) || *text != 0 || frames == 0 ||
		start > (MH_U32_MAX - base) / stride ||
		frames > MH_U32_MAX / stride)
		return 0;

	offset = base + start * stride;
	length = frames * stride;

	if (offset != layout->image_size || length > MH_U32_MAX - offset)
		return 0;

	strcpy(layout->tracks[track].file, name);
	layout->tracks[track].offset = offset;
	layout->tracks[track].length = length;
	layout->image_size = offset + length;
	reader->file = 1;

	return 1;
}

static int
track_complete(const struct toc_reader *reader)
{
	return reader->file && reader->silence == reader->start;
}

static int
begin_track(struct toc_reader *reader, const char *mode)
{
	if (reader->tracks >= MH_RETAIL_DISC_TRACKS ||
		(reader->tracks != 0 && !track_complete(reader)))
		return 0;

	if (reader->tracks == 0) {
		if (strcmp(mode, "MODE1") == 0)
			reader->layout.data_format = MH_DISC_ISO;
		else if (strcmp(mode, "MODE1_RAW") == 0)
			reader->layout.data_format = MH_DISC_MODE1;
		else
			return 0;
	} else if (strcmp(mode, "AUDIO") != 0)
		return 0;

	reader->tracks++;
	reader->file = 0;
	reader->silence = 0;
	reader->start = 0;

	return 1;
}

static int
read_pregap(struct toc_reader *reader, const char *text, int start)
{
	unsigned long frames;

	/* The generated two-second pregap is not stored in the BIN file. */
	if (reader->tracks != 2 || !read_frames(&text, &frames) || *text != 0 ||
		frames != CD_PREGAP_FRAMES)
		return 0;

	if (start) {
		if (!reader->file || !reader->silence || reader->start)
			return 0;

		reader->start = 1;
	} else {
		if (reader->file || reader->silence)
			return 0;

		reader->silence = 1;
	}

	return 1;
}

static int
read_line(struct toc_reader *reader, const char *line)
{
	if (!reader->header) {
		reader->header = strcmp(line, "CD_ROM") == 0;
		return reader->header;
	}

	if (strncmp(line, "TRACK ", 6) == 0)
		return begin_track(reader, line + 6);

	if (reader->tracks == 0)
		return 0;

	if (strcmp(line, "NO COPY") == 0)
		return 1;

	if (reader->tracks > 1 &&
		(strcmp(line, "NO PRE_EMPHASIS") == 0 ||
			strcmp(line, "TWO_CHANNEL_AUDIO") == 0))
		return 1;

	if (strncmp(line, "DATAFILE ", 9) == 0)
		return read_file(reader, line + 9, 0);

	if (strncmp(line, "FILE ", 5) == 0)
		return read_file(reader, line + 5, 1);

	if (strncmp(line, "SILENCE ", 8) == 0)
		return read_pregap(reader, line + 8, 0);

	if (strncmp(line, "START ", 6) == 0)
		return read_pregap(reader, line + 6, 1);

	return 0;
}

static void
trim_line(char *line)
{
	size_t i, end;
	int quoted;

	quoted = 0;

	for (i = 0; line[i] != 0; i++) {
		if (line[i] == '"')
			quoted = !quoted;

		if (!quoted && line[i] == '/' && line[i + 1] == '/')
			break;
	}

	end = i;

	while (end > 0 &&
		(line[end - 1] == ' ' || line[end - 1] == '\t' ||
			line[end - 1] == '\r'))
		end--;

	line[end] = 0;
}

enum mh_error_code
mh_cdrdao_parse(const void *data, size_t size, struct mh_disc_layout *output,
	struct mh_error *error_out)
{
	struct toc_reader reader;
	const unsigned char *bytes;
	const char *text;
	char line[MH_DISC_TRACK_NAME_SIZE + 128];
	size_t offset, length;

	if (data == NULL || output == NULL || size == 0 ||
		size > MH_DISC_LAYOUT_TEXT_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid cdrdao TOC text");

	memset(&reader, 0, sizeof(reader));
	reader.layout.big_endian_audio = 1;
	bytes = data;
	offset = 0;

	while (offset < size) {
		length = 0;

		while (offset < size && bytes[offset] != '\n') {
			if (bytes[offset] == 0 || length >= sizeof(line) - 1)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or oversized cdrdao TOC line");

			line[length++] = (char)bytes[offset++];
		}

		if (offset < size)
			offset++;

		line[length] = 0;
		trim_line(line);
		text = line;
		skip_spaces(&text);

		if (*text != 0 && !read_line(&reader, text))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported cdrdao track layout or invalid byte range");
	}

	if (reader.tracks != MH_RETAIL_DISC_TRACKS || !track_complete(&reader))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected one data track and ten audio tracks in the cdrdao image");

	*output = reader.layout;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
