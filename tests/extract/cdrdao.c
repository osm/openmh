#include <stdio.h>
#include <string.h>

#include "engine/assets/prepare_disc_layout.h"

static int failures;

/* Two cooked data sectors followed by ten one-sector audio tracks. */
static char toc[2048];
static const char *const toc_parts[] = {
	"CD_ROM\n\n// Track 1\nTRACK MODE1\nNO COPY\n"
	"DATAFILE \"disc image.bin\" 00:00:02 // bytes: 4096\n"
	"TRACK AUDIO\nNO COPY\nNO PRE_EMPHASIS\nTWO_CHANNEL_AUDIO\n"
	"SILENCE 00:02:00\nFILE \"disc image.bin\" #4096 0 00:00:01\n"
	"START 00:02:00\n",
	"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:01 00:00:01\n"
	"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:02 00:00:01\n"
	"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:03 00:00:01\n"
	"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:04 00:00:01\n",
	"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:05 00:00:01\n"
	"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:06 00:00:01\n"
	"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:07 00:00:01\n"
	"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:08 00:00:01\n"
	"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:09 00:00:01\n"};

struct rejection_case {
	const char	*from, *to, *description;
};

static void
check(int success, const char *message)
{
	if (!success) {
		fprintf(stderr, "%s\n", message);
		failures++;
	}
}

static void
replace_text(char *text, const char *from, const char *to)
{
	char *position;
	size_t old_length, new_length;

	position = strstr(text, from);
	check(position != NULL, "fixture contains replacement target");

	if (position == NULL)
		return;

	old_length = strlen(from);
	new_length = strlen(to);
	memmove(position + new_length, position + old_length,
		strlen(position + old_length) + 1);
	memcpy(position, to, new_length);
}

static void
check_layout(void)
{
	static const unsigned long offsets[] = {0, 4096, 6448, 8800, 11152,
		13504, 15856, 18208, 20560, 22912, 25264};

	struct mh_disc_layout layout;
	struct mh_error error;
	unsigned int i;

	memset(&layout, 0, sizeof(layout));
	check(mh_disc_layout_parse(toc, strlen(toc), &layout, &error) ==
			MH_ERROR_NONE,
		"parse cooked cdrdao image");
	check(layout.data_format == MH_DISC_ISO && layout.big_endian_audio &&
			layout.image_size == 27616,
		"cooked sectors and big-endian audio");

	for (i = 0; i < MH_RETAIL_DISC_TRACKS; i++) {
		check(strcmp(layout.tracks[i].file, "disc image.bin") == 0 &&
				layout.tracks[i].offset == offsets[i] &&
				layout.tracks[i].length ==
					(i == 0 ? 4096UL : 2352UL),
			"track ranges exclude generated pregap and adjacent tracks");
	}
}

static void
check_raw_and_line_endings(void)
{
	char text[4096], lines[8192], *out;
	const char *in;
	struct mh_disc_layout layout;
	struct mh_error error;

	strcpy(text, toc);
	replace_text(text, "TRACK MODE1", "TRACK MODE1_RAW");

	while (strstr(text, "#4096") != NULL)
		replace_text(text, "#4096", "#4704");

	out = lines;
	*out++ = '\t';

	for (in = text; *in != 0; in++) {
		if (*in == '\n') {
			*out++ = ' ';
			*out++ = '\r';
		}

		*out++ = *in;
	}

	*out = 0;
	memset(&layout, 0, sizeof(layout));
	check(mh_disc_layout_parse(lines, strlen(lines), &layout, &error) ==
				MH_ERROR_NONE &&
			layout.data_format == MH_DISC_MODE1 &&
			layout.tracks[0].length == 4704 &&
			layout.tracks[1].offset == 4704 &&
			layout.tracks[10].offset == 25872 &&
			layout.image_size == 28224,
		"raw data sectors with CRLF, leading whitespace and comments");
}

static void
check_rejections(void)
{
	static const struct rejection_case cases[] = {
		{"TRACK MODE1", "TRACK MODE2", "unsupported data mode"},
		{"NO PRE_EMPHASIS", "PRE_EMPHASIS", "unsupported emphasis"},
		{"TWO_CHANNEL_AUDIO", "FOUR_CHANNEL_AUDIO",
			"unsupported channels"},
		{"#4096", "#4095", "overlapping tracks"},
		{"#4096", "#4097", "gap in stored tracks"},
		{"#4096", "#4294967296", "byte offset overflow"},
		{"#4096", "#999999999999999999999999", "oversized number"},
		{"#4096 0", "#4096 999999999:00:00",
			"time multiplication overflow"},
		{"#4096 0", "#4096 00:60:00", "invalid seconds"},
		{"#4096 0", "#4096 00:00:75", "invalid frames"},
		{"#4096 0 00:00:01", "#4096 0 00:00:00", "empty track"},
		{"#4096 0 00:00:01", "#4096 0 999999:00:00",
			"byte length overflow"},
		{"#4096 0 00:00:01", "#4096 0 00:00:01 junk",
			"trailing tokens"},
		{"SILENCE 00:02:00\n", "", "START without SILENCE"},
		{"START 00:02:00\n", "", "SILENCE without START"},
		{"START 00:02:00", "START 00:01:00", "mismatched pregap"},
		{"SILENCE 00:02:00", "SILENCE 00:02:00\nSILENCE 00:02:00",
			"duplicate pregap"},
		{"\"disc image.bin\"", "\"../outside.bin\"", "path traversal"},
		{"\"disc image.bin\"", "\"/outside.bin\"", "absolute path"},
		{"FILE \"disc image.bin\" #4096", "FILE \"other.bin\" #4096",
			"mixed files"},
		{"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:09 00:00:01\n",
			"", "missing final track"}};

	struct mh_disc_layout layout, before;
	struct mh_error error;
	char text[4096];
	size_t i;
	enum mh_error_code status;

	memset(&before, 0x5a, sizeof(before));

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		strcpy(text, toc);
		replace_text(text, cases[i].from, cases[i].to);
		layout = before;
		status = mh_disc_layout_parse(
			text, strlen(text), &layout, &error);
		check(status != MH_ERROR_NONE &&
				memcmp(&before, &layout, sizeof(layout)) == 0,
			cases[i].description);
	}

	strcpy(text, toc);
	strcat(text,
		"TRACK AUDIO\nFILE \"disc image.bin\" #4096 00:00:10 00:00:01\n");
	check(mh_disc_layout_parse(text, strlen(text), &layout, &error) !=
			MH_ERROR_NONE,
		"reject extra track");
	strcpy(text, toc);
	text[20] = 0;
	check(mh_disc_layout_parse(text, strlen(toc), &layout, &error) !=
			MH_ERROR_NONE,
		"reject embedded NUL");
}

static void
check_truncated_text(void)
{
	struct mh_disc_layout layout, before;
	struct mh_error error;
	size_t size;

	memset(&before, 0x5a, sizeof(before));

	for (size = 0; size < strlen(toc) - 1; size++) {
		layout = before;
		check(mh_disc_layout_parse(toc, size, &layout, &error) !=
					MH_ERROR_NONE &&
				memcmp(&before, &layout, sizeof(layout)) == 0,
			"truncated TOC preserves previous layout");
	}

	check(mh_disc_layout_parse(toc, strlen(toc) - 1, &layout, &error) ==
			MH_ERROR_NONE,
		"final newline is optional");
}

int
main(void)
{
	strcpy(toc, toc_parts[0]);
	strcat(toc, toc_parts[1]);
	strcat(toc, toc_parts[2]);

	check_layout();
	check_raw_and_line_endings();
	check_rejections();
	check_truncated_text();

	return failures != 0;
}
