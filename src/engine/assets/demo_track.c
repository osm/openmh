#include <stdlib.h>
#include <string.h>

#include "engine/assets/demo_definition.h"
#include "engine/assets/demo_track.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track.h"
#include "engine/core/binary.h"

#define AI_OFFSET 0x1308
#define PATH_LIST_BYTES 1024
#define RECORD_PATH_BYTES 256
#define SKY_CD_TRACK_OFFSET 28

struct track_layout {
	size_t		record_bytes, path_shift, grid, sky, view;
	unsigned int	ai_values;
	int		goal_camera, texture_brightness;
};

/* Per-edition record layouts, decoded without native struct casts. */
static const struct track_layout software_first = {
	0x1fac, /* Record bytes. */
	0,	/* Path-field shift. */
	0x1420, /* Grid offset. */
	0x1d40, /* Sky offset. */
	0x1f6c, /* View offset. */
	10,	/* AI tuning values. */
	0,	/* Goal camera present. */
	0,	/* Texture brightness present. */
};

static const struct track_layout software_second = {
	0x1fc8, /* Record bytes. */
	0,	/* Path-field shift. */
	0x143c, /* Grid offset. */
	0x1d5c, /* Sky offset. */
	0x1f88, /* View offset. */
	11,	/* AI tuning values. */
	0,	/* Goal camera present. */
	0,	/* Texture brightness present. */
};

static const struct track_layout glide = {
	0x1ff0, /* Record bytes. */
	0,	/* Path-field shift. */
	0x1458, /* Grid offset. */
	0x1d78, /* Sky offset. */
	0x1fa4, /* View offset. */
	11,	/* AI tuning values. */
	1,	/* Goal camera present. */
	1,	/* Texture brightness present. */
};

static const struct track_layout mplayer = {
	0x20f0, /* Record bytes. */
	0x100,	/* Path-field shift. */
	0x1558, /* Grid offset. */
	0x1e78, /* Sky offset. */
	0x20a4, /* View offset. */
	11,	/* AI tuning values. */
	1,	/* Goal camera present. */
	1,	/* Texture brightness present. */
};

static const struct track_layout *
find_layout(enum mh_demo_track_layout kind)
{
	switch (kind) {
	case MH_DEMO_TRACK_SOFTWARE_FIRST:
		return &software_first;
	case MH_DEMO_TRACK_SOFTWARE_SECOND:
		return &software_second;
	case MH_DEMO_TRACK_GLIDE:
		return &glide;
	case MH_DEMO_TRACK_MPLAYER:
		return &mplayer;
	default:
		return NULL;
	}
}

static const struct mh_definition_field descriptions[] = {
	{"Division", 0, MH_DEFINITION_INTEGER, 1},
	{"BasePath", 8, MH_DEFINITION_DIRECTORY, 1},
	{"TrackName", 0x108, MH_DEFINITION_STRING, 1},
	{"Location", 0x208, MH_DEFINITION_STRING, 1},
	{"Length", 0x308, MH_DEFINITION_STRING, 1}};

static const struct mh_definition_field paths[] = {
	{"CollisionName", 0x508, MH_DEFINITION_STRING, 1},
	{"MaterialsName", 0x608, MH_DEFINITION_STRING, 1},
	{"SplineName", 0x808, MH_DEFINITION_STRING, 1},
	{"CameraSplineName", 0x908, MH_DEFINITION_STRING, 1},
	{"BackgroundName", 0xa08, MH_DEFINITION_STRING, 1},
	{"SceneName", 0xb08, MH_DEFINITION_STRING, 1},
	{"AITrackName", 0xc08, MH_DEFINITION_DIRECTORY, 1},
	{"EnvSoundName", 0xd08, MH_DEFINITION_STRING, 1},
	{"EnvMapName", 0xe08, MH_DEFINITION_STRING, 1},
	{"EnvMapName", 0xf08, MH_DEFINITION_STRING, 1},
	{"EnvMapName", 0x1008, MH_DEFINITION_STRING, 1},
	{"StartSpline1", 0x1108, MH_DEFINITION_STRING, 1},
	{"StartSpline2", 0x1208, MH_DEFINITION_STRING, 1}};

static const struct mh_definition_field grid_fields[] = {
	{"StartGrid", 0, MH_DEFINITION_FLOAT, 4},
	{"TrackMapName", 16, MH_DEFINITION_STRING, 1},
	{"TrackMapInfo", 272, MH_DEFINITION_FLOAT, 4}};

static const struct mh_definition_field sky_fields[] = {
	{"TopColour", 0, MH_DEFINITION_BYTES_RGB, 3},
	{"BottomColour", 4, MH_DEFINITION_BYTES_RGB, 3},
	{"Horizon", 8, MH_DEFINITION_FLOAT, 1},
	{"Laps", 36, MH_DEFINITION_INTEGER, 1},
	{"LineObjectName", 40, MH_DEFINITION_STRING, 1}};

static const struct mh_definition_field view_fields[] = {
	{"ViewDistance", 0, MH_DEFINITION_FLOAT, 1},
	{"WorldBrightness", 4, MH_DEFINITION_FLOAT, 3},
	{"ObjectBrightness", 16, MH_DEFINITION_FLOAT, 3},
	{"ObjectAmbient", 28, MH_DEFINITION_FLOAT, 3},
	{"CueColour", 40, MH_DEFINITION_FLOAT, 3}};

static const struct mh_definition_field tail_fields[] = {
	{"CueStart", 0, MH_DEFINITION_FLOAT, 1},
	{"SpecularFactor", 4, MH_DEFINITION_FLOAT, 1},
	{"LightIntensity", 8, MH_DEFINITION_FLOAT, 1}};

static enum mh_error_code
write_fields(struct mh_definition_text *text, const unsigned char *record,
	const struct mh_definition_field *fields, size_t count,
	struct mh_error *error_out)
{
	size_t i;
	enum mh_error_code status;

	for (i = 0; i < count; i++) {
		status = mh_definition_write(
			text, record, &fields[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
write_world(struct mh_definition_text *text, const unsigned char *record,
	enum mh_demo_track_layout kind, const struct track_layout *layout,
	struct mh_error *error_out)
{
	static const struct mh_definition_field field = {
		"FileName", 0, MH_DEFINITION_STRING, 1};

	const unsigned char *world, *base, *end;
	size_t length;

	world = record + 0x408 + layout->path_shift;

	if (kind == MH_DEMO_TRACK_SOFTWARE_FIRST) {
		/*
		 * Its loader uses a root-relative world name; ours joins
		 * BasePath.
		 */
		base = record + 8;
		end = memchr(base, 0, RECORD_PATH_BYTES);

		if (end == NULL || memchr(world, 0, RECORD_PATH_BYTES) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated demo world path");

		length = (size_t)(end - base);

		if (length == 0 ||
			(base[length - 1] != '\\' && base[length - 1] != '/') ||
			memcmp(base, world, length) != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"demo world path is outside its BasePath");

		world += length;
	}

	return mh_definition_write(text, world, &field, error_out);
}

static enum mh_error_code
write_tuning(struct mh_definition_text *text, const unsigned char *record,
	const struct track_layout *layout, struct mh_error *error_out)
{
	static const char *const names[] = {"AI1", "AI2", "AI3"};
	static const struct mh_definition_field goal[] = {
		{"AIGoalWheel", 0, MH_DEFINITION_FLOAT, 1},
		{"GoalCamera", 4, MH_DEFINITION_FLOAT, 6}};

	struct mh_definition_field field;
	unsigned char values[MH_TRACK_AI_VALUES * 4];
	unsigned int i;
	enum mh_error_code status;

	field.offset = 0;
	field.kind = MH_DEFINITION_FLOAT;
	field.count = MH_TRACK_AI_VALUES;

	for (i = 0; i < MH_TRACK_DIFFICULTIES; i++) {
		field.name = names[i];

		/*
		 * The first software demo has no authored catch-up parameter.
		 */
		memset(values, 0, sizeof(values));
		memcpy(values,
			record + AI_OFFSET + layout->path_shift +
				i * layout->ai_values * 4,
			layout->ai_values * 4);
		status = mh_definition_write(text, values, &field, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (layout->goal_camera) {
		status = write_fields(text, record + layout->grid - 28, goal,
			sizeof(goal) / sizeof(goal[0]), error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return write_fields(text, record + layout->grid, grid_fields,
		sizeof(grid_fields) / sizeof(grid_fields[0]), error_out);
}

static enum mh_error_code
write_visuals(struct mh_definition_text *text, const unsigned char *record,
	const struct track_layout *layout, struct mh_error *error_out)
{
	static const struct mh_definition_field texture = {
		"3DAccBrightness", 52, MH_DEFINITION_FLOAT, 3};

	size_t halo;
	enum mh_error_code status;

	halo = layout->grid + 288;
	status = mh_definition_paths(
		text, record + halo, PATH_LIST_BYTES, "Halo", error_out);

	if (status == MH_ERROR_NONE)
		status = mh_definition_paths(text,
			record + halo + PATH_LIST_BYTES, PATH_LIST_BYTES,
			"ZeroTransparent", error_out);

	if (status == MH_ERROR_NONE)
		status = write_fields(text, record + layout->sky, sky_fields,
			sizeof(sky_fields) / sizeof(sky_fields[0]), error_out);

	if (status == MH_ERROR_NONE)
		status = write_fields(text, record + layout->view, view_fields,
			sizeof(view_fields) / sizeof(view_fields[0]),
			error_out);

	if (status == MH_ERROR_NONE && layout->texture_brightness)
		status = mh_definition_write(
			text, record + layout->view, &texture, error_out);

	if (status == MH_ERROR_NONE)
		status = write_fields(text, record + layout->record_bytes - 12,
			tail_fields,
			sizeof(tail_fields) / sizeof(tail_fields[0]),
			error_out);

	return status;
}

static enum mh_error_code
read_record(const unsigned char *record, enum mh_demo_track_layout kind,
	const struct track_layout *layout, struct mh_definition_text *output,
	struct mh_error *error_out)
{
	static const struct mh_definition_field length_us = {
		"LengthUS", 0x408, MH_DEFINITION_STRING, 1};

	static const struct mh_definition_field music = {
		"CDTrack", 0, MH_DEFINITION_INTEGER, 1};

	struct mh_definition_text text;
	unsigned char cd_track[4];
	enum mh_error_code status;

	memset(&text, 0, sizeof(text));
	status = write_fields(&text, record, descriptions,
		sizeof(descriptions) / sizeof(descriptions[0]), error_out);

	if (status == MH_ERROR_NONE && kind == MH_DEMO_TRACK_MPLAYER &&
		record[0x408] != 0)
		status = mh_definition_write(
			&text, record, &length_us, error_out);

	if (status == MH_ERROR_NONE)
		status = write_world(&text, record, kind, layout, error_out);

	if (status == MH_ERROR_NONE)
		status = write_fields(&text, record + layout->path_shift, paths,
			sizeof(paths) / sizeof(paths[0]), error_out);

	if (status == MH_ERROR_NONE)
		status = write_tuning(&text, record, layout, error_out);

	if (status == MH_ERROR_NONE)
		status = write_visuals(&text, record, layout, error_out);

	mh_write_u32le(cd_track,
		kind == MH_DEMO_TRACK_MPLAYER
			? 4
			: mh_read_u32le(
				record + layout->sky + SKY_CD_TRACK_OFFSET));

	if (status == MH_ERROR_NONE)
		status =
			mh_definition_write(&text, cd_track, &music, error_out);

	if (status == MH_ERROR_NONE)
		*output = text;

	return status;
}

enum mh_error_code
mh_track_demo_definition(const struct mh_file *source,
	enum mh_demo_track_layout kind, struct mh_definition_text *output,
	struct mh_error *error_out)
{
	const struct track_layout *layout;
	unsigned char *record;
	size_t size;
	enum mh_error_code status;

	layout = find_layout(kind);

	if (source == NULL || output == NULL || layout == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo track preparation request");

	if (source->size != layout->record_bytes + 4)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected wrapped demo track record size");

	status = mh_tbl_decode(
		source->data, source->size, &record, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = read_record(record, kind, layout, output, error_out);
	free(record);

	return status;
}

enum mh_error_code
mh_track_demo_decode(const struct mh_file *source,
	enum mh_demo_track_layout layout, struct mh_track *output,
	struct mh_error *error_out)
{
	struct mh_definition_text definition;
	enum mh_error_code status;

	status = mh_track_demo_definition(
		source, layout, &definition, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_track_config_trk_decode(
		definition.data, definition.size, output, error_out);
}
