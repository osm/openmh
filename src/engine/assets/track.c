#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/config_line.h"
#include "engine/assets/track.h"
#include "engine/catalog/halo_paths.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/race_limits.h"

#define CONFIG_LINE_BYTES 1024
#define VALUE_LIMIT 100.0
#define MAX_CD_TRACK 99
#define MAP_COORDINATE_LIMIT 1000000
#define VIEW_DISTANCE_LIMIT 1000000

enum {
	SKY_TOP = 1,
	SKY_BOTTOM = 2,
	SKY_HORIZON = 4,
	LIGHT_WORLD = 1,
	LIGHT_OBJECT = 2,
	LIGHT_AMBIENT = 4,
	LIGHT_INTENSITY = 8,
	LIGHT_TEXTURE = 16,
	LIGHT_SPECULAR = 32
};

static int
read_values(const char *text, double *values, unsigned int count)
{
	char *end;
	unsigned int i;

	for (i = 0; i < count; i++) {
		errno = 0;
		values[i] = strtod(text, &end);

		if (errno != 0 || end == text ||
			!(values[i] >= 0.0 && values[i] <= VALUE_LIMIT)) {
			return 0;
		}

		if (i + 1 < count && *end != ' ' && *end != '\t')
			return 0;

		text = end;
	}

	while (*text == ' ' || *text == '\t')
		text++;

	return *text == 0;
}

static enum mh_error_code
read_line(const unsigned char *text, size_t size, size_t *position,
	char line[CONFIG_LINE_BYTES], struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_config_line_read(
		text, size, position, line, CONFIG_LINE_BYTES, 126, 0);

	if (status == MH_ERROR_UNSUPPORTED)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"track configuration contains non-ASCII text");

	if (status == MH_ERROR_RANGE)
		return mh_error_set(
			error_out, status, "track configuration line too long");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_map_info(const char *arguments, struct mh_track *track,
	struct mh_error *error_out)
{
	size_t i;
	double number;
	char *value_end;
	const char *value;

	if (track->has_map_info)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "duplicate TrackMapInfo");

	value = arguments;

	for (i = 0; i < 4; i++) {
		errno = 0;
		number = strtod(value, &value_end);

		if (errno != 0 || value_end == value ||
			!(number >= -MAP_COORDINATE_LIMIT &&
				number <= MAP_COORDINATE_LIMIT) ||
			(i < 3 && *value_end != ' ' && *value_end != '\t'))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid TrackMapInfo (expected four finite coordinates)");

		track->map_info[i] = (float)number;
		value = value_end;
	}

	while (*value == ' ' || *value == '\t')
		value++;

	if (*value != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid TrackMapInfo suffix");

	track->has_map_info = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_sky(const char *key, const char *arguments, struct mh_track *track,
	struct mh_error *error_out)
{
	size_t i;
	double number;
	char *value_end;
	unsigned int flag, channels;
	const char *value;

	flag = strcmp(key, "topcolour") == 0	   ? SKY_TOP
		: strcmp(key, "bottomcolour") == 0 ? SKY_BOTTOM
						: SKY_HORIZON;
	channels = flag == SKY_HORIZON ? 1 : 3;

	if (track->sky_settings & flag)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "duplicate sky setting");

	value = arguments;

	for (i = 0; i < channels; i++) {
		errno = 0;
		number = strtod(value, &value_end);

		if (errno != 0 || value == value_end ||
			!(number >= 0 &&
				number <= (flag == SKY_HORIZON ? 1 : 255)) ||
			(flag != SKY_HORIZON &&
				number != (unsigned int)number) ||
			(i + 1 < channels && *value_end != ' ' &&
				*value_end != '\t'))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid sky setting");

		if (flag == SKY_HORIZON)
			track->horizon = (double)(float)number;
		else if (flag == SKY_TOP)
			track->sky_top[i] = (unsigned char)number;
		else
			track->sky_bottom[i] = (unsigned char)number;
		value = value_end;
	}

	while (*value == ' ' || *value == '\t')
		value++;

	if (*value != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sky setting suffix");

	track->sky_settings |= flag;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_fog(const char *key, const char *arguments, struct mh_track *track,
	struct mh_error *error_out)
{
	size_t i;
	double number;
	char *value_end;
	const char *value;
	unsigned int channels;

	channels = strcmp(key, "cuecolour") == 0 ? 3 : 1;

	if ((channels == 3 && track->has_cue_color) ||
		(channels == 1 && track->has_cue_start))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"duplicate track fog setting");

	value = arguments;

	for (i = 0; i < channels; i++) {
		errno = 0;
		number = strtod(value, &value_end);

		if (errno != 0 || value_end == value ||
			!(number >= -FLT_MAX && number <= FLT_MAX) ||
			(i + 1 < channels && *value_end != ' ' &&
				*value_end != '\t'))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid track fog setting (expected finite values)");

		if (channels == 3)
			track->cue_color[i] = (double)(float)number;
		else
			track->cue_start = (double)(float)number;
		value = value_end;
	}

	while (*value == ' ' || *value == '\t')
		value++;

	if (*value != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track fog setting suffix");

	if (channels == 3)
		track->has_cue_color = 1;
	else
		track->has_cue_start = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_goal_camera(const char *arguments, struct mh_track *track,
	struct mh_error *error_out)
{
	size_t i;
	double number;
	char *value_end;
	const char *value;

	if (track->has_goal_camera)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "duplicate GoalCamera");

	value = arguments;

	for (i = 0; i < 6; i++) {
		errno = 0;
		number = strtod(value, &value_end);

		if (errno != 0 || value_end == value ||
			!(number >= -FLT_MAX && number <= FLT_MAX) ||
			(i < 5 && *value_end != ' ' && *value_end != '\t'))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid GoalCamera (expected six finite coordinates)");

		track->goal_camera[i] = (double)(float)number;
		value = value_end;
	}

	while (*value == ' ' || *value == '\t')
		value++;

	if (*value != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid GoalCamera suffix");

	track->has_goal_camera = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static char *
description_field(const char *key, struct mh_track *track)
{
	if (strcmp(key, "location") == 0)
		return track->location;

	if (strcmp(key, "length") == 0)
		return track->length;

	if (strcmp(key, "lengthus") == 0)
		return track->length_us;

	return NULL;
}

static enum mh_error_code
read_description(const char *arguments, char text[MH_TRACK_TEXT_SIZE],
	struct mh_error *error_out)
{
	char first[MH_TRACK_TEXT_SIZE], second[MH_TRACK_TEXT_SIZE];
	size_t first_size, second_size;
	int count;

	/* Keep at most two whitespace-separated words. */
	second[0] = 0;
	count = sscanf(arguments, "%255s %255s", first, second);

	if (text[0] != 0 || count < 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or duplicate track description");

	if (second[0] == '/' || (second[0] != 0 && second[1] == '/'))
		second[0] = 0;

	first_size = strlen(first);
	second_size = strlen(second);

	if (first_size + second_size + (second_size != 0) >= MH_TRACK_TEXT_SIZE)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"track description is too long");

	memcpy(text, first, first_size + 1);

	if (second_size != 0) {
		text[first_size] = ' ';
		memcpy(text + first_size + 1, second, second_size + 1);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static char *
path_field(const char *key, struct mh_track *track)
{
	if (strcmp(key, "backgroundname") == 0)
		return track->background;

	if (strcmp(key, "trackmapname") == 0)
		return track->map_name;

	if (strcmp(key, "lineobjectname") == 0)
		return track->line_object;

	if (strcmp(key, "basepath") == 0)
		return track->base_path;

	if (strcmp(key, "filename") == 0)
		return track->world_file;

	if (strcmp(key, "scenename") == 0)
		return track->scene_file;

	if (strcmp(key, "collisionname") == 0)
		return track->collision_file;

	if (strcmp(key, "materialsname") == 0)
		return track->materials_file;

	if (strcmp(key, "splinename") == 0)
		return track->spline_file;

	if (strcmp(key, "camerasplinename") == 0)
		return track->camera_file;

	if (strcmp(key, "startspline1") == 0)
		return track->start_camera[0];

	if (strcmp(key, "startspline2") == 0)
		return track->start_camera[1];

	if (strcmp(key, "aitrackname") == 0)
		return track->ai_path;

	if (strcmp(key, "envsoundname") == 0)
		return track->env_sound_file;

	return NULL;
}

static double *
lighting_field(const char *key, struct mh_track *track, unsigned int *flag,
	unsigned int *count)
{
	double *lighting;

	lighting = NULL;
	*flag = 0;
	*count = 3;

	if (strcmp(key, "worldbrightness") == 0) {
		lighting = track->world_brightness;
		*flag = LIGHT_WORLD;
	} else if (strcmp(key, "objectbrightness") == 0) {
		lighting = track->object_brightness;
		*flag = LIGHT_OBJECT;
	} else if (strcmp(key, "objectambient") == 0) {
		lighting = track->object_ambient;
		*flag = LIGHT_AMBIENT;
	} else if (strcmp(key, "lightintensity") == 0) {
		lighting = &track->light_intensity;
		*flag = LIGHT_INTENSITY;
		*count = 1;
	} else if (strcmp(key, "specularfactor") == 0) {
		lighting = &track->specular_factor;
		*flag = LIGHT_SPECULAR;
		*count = 1;
	} else if (strcmp(key, "3daccbrightness") == 0) {
		lighting = track->texture_brightness;
		*flag = LIGHT_TEXTURE;
	}

	return lighting;
}

static enum mh_error_code
read_lighting(const char *arguments, double *lighting, unsigned int flag,
	unsigned int count, unsigned int *seen, struct mh_error *error_out)
{
	size_t i;

	if ((*seen & flag) != 0 || !read_values(arguments, lighting, count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or duplicate track lighting setting (expected values in 0..100)");

	*seen |= flag;

	if (flag == LIGHT_TEXTURE || flag == LIGHT_SPECULAR)
		for (i = 0; i < count; i++)
			lighting[i] = (double)(float)lighting[i];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_track_config_trk_decode(const unsigned char *text, size_t size,
	struct mh_track *track, struct mh_error *error_out)
{
	struct mh_track parsed;
	char line[CONFIG_LINE_BYTES];
	char key[64];
	char path[MH_TRACK_PATH_SIZE];
	char *destination;
	char *value_end;
	size_t position;
	size_t i;
	unsigned int environment_count;
	int key_end;
	int path_end;
	unsigned int lighting_seen;
	unsigned int lighting_flag;
	unsigned int lighting_count;
	double *lighting;
	double number;
	enum mh_error_code status;
	const char *argument;
	unsigned long division;
	int cd_seen;

	memset(&parsed, 0, sizeof(parsed));

	for (i = 0; i < 3; i++) {
		parsed.world_brightness[i] = 1.0;
		parsed.object_brightness[i] = 1.0;
		parsed.texture_brightness[i] = 1.0;
	}

	parsed.light_intensity = 1.0;
	parsed.specular_factor = 1.0;
	lighting_seen = 0;
	position = 0;
	environment_count = 0;
	cd_seen = 0;

	while (position < size) {
		status = read_line(text, size, &position, line, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		key_end = 0;

		if (sscanf(line, "%63s%n", key, &key_end) != 1)
			continue;

		for (i = 0; key[i] != 0; i++) {
			if (key[i] >= 'A' && key[i] <= 'Z')
				key[i] += 'a' - 'A';
		}

		destination = description_field(key, &parsed);

		if (destination != NULL) {
			status = read_description(
				line + key_end, destination, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		if (strcmp(key, "trackname") == 0) {
			path_end = 0;

			if (parsed.name[0] != 0 ||
				sscanf(line + key_end, "%127s %n", parsed.name,
					&path_end) != 1 ||
				line[key_end + path_end] != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate TrackName");

			continue;
		}

		if (strcmp(key, "halo") == 0) {
			status = mh_halo_paths_append(
				&parsed.halos, line + key_end, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		if (strcmp(key, "trackmapinfo") == 0) {
			status = read_map_info(
				line + key_end, &parsed, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		if (strcmp(key, "zerotransparent") == 0) {
			path_end = 0;

			if (sscanf(line + key_end, "%255s %n", path,
				&path_end) != 1 ||
				line[key_end + path_end] != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid ZeroTransparent name");

			i = strlen(path) + 1;

			if (i >= sizeof(parsed.zero_transparent) -
					parsed.zero_transparent_size)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"ZeroTransparent list is too long");

			memcpy(parsed.zero_transparent +
					parsed.zero_transparent_size,
				path, i);
			parsed.zero_transparent_size += i;
			continue;
		}

		if (strcmp(key, "topcolour") == 0 ||
			strcmp(key, "bottomcolour") == 0 ||
			strcmp(key, "horizon") == 0) {
			status = read_sky(
				key, line + key_end, &parsed, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		lighting = lighting_field(
			key, &parsed, &lighting_flag, &lighting_count);

		if (lighting != NULL) {
			status = read_lighting(line + key_end, lighting,
				lighting_flag, lighting_count, &lighting_seen,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		if (strcmp(key, "division") == 0) {
			argument = line + key_end;

			while (*argument == ' ' || *argument == '\t')
				argument++;

			if (parsed.has_division || *argument < '0' ||
				*argument > '9')
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate Division");

			errno = 0;
			division = strtoul(argument, &value_end, 10);

			if (errno != 0 || division > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Division");

			while (*value_end == ' ' || *value_end == '\t')
				value_end++;

			if (*value_end != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Division suffix");

			parsed.has_division = 1;
			parsed.division = division;
			continue;
		}

		if (strcmp(key, "cuecolour") == 0 ||
			strcmp(key, "cuestart") == 0) {
			status = read_fog(
				key, line + key_end, &parsed, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		if (strcmp(key, "viewdistance") == 0) {
			errno = 0;
			number = strtod(line + key_end, &value_end);

			if (parsed.view_distance != 0 || errno != 0 ||
				value_end == line + key_end ||
				!(number > 0 &&
					number <= VIEW_DISTANCE_LIMIT) ||
				(float)number == 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate ViewDistance");

			while (*value_end == ' ' || *value_end == '\t')
				value_end++;

			if (*value_end != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid ViewDistance suffix");

			parsed.view_distance = (double)(float)number;
			continue;
		}

		if (strcmp(key, "aigoalwheel") == 0) {
			errno = 0;
			number = strtod(line + key_end, &value_end);

			if (parsed.has_ai_goal_wheel || errno != 0 ||
				value_end == line + key_end ||
				!(number >= -FLT_MAX && number <= FLT_MAX))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate AIGoalWheel");

			while (*value_end == ' ' || *value_end == '\t')
				value_end++;

			if (*value_end != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid AIGoalWheel suffix");

			parsed.has_ai_goal_wheel = 1;
			parsed.ai_goal_wheel = (float)number;
			continue;
		}

		if (strcmp(key, "goalcamera") == 0) {
			status = read_goal_camera(
				line + key_end, &parsed, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		if (strcmp(key, "startgrid") == 0) {
			if (parsed.has_start_grid ||
				!read_values(
					line + key_end, parsed.start_grid, 4)) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate StartGrid (expected four values in 0..100)");
			}

			parsed.has_start_grid = 1;
			continue;
		}

		if (strcmp(key, "laps") == 0) {
			if (parsed.laps != 0 ||
				!read_values(line + key_end, &number, 1) ||
				number < MH_RACE_MIN_LAPS ||
				number > MH_RACE_MAX_LAPS ||
				number != (unsigned int)number)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate Laps");

			parsed.laps = (unsigned int)number;
			continue;
		}

		if (strcmp(key, "cdtrack") == 0) {
			if (cd_seen ||
				!read_values(line + key_end, &number, 1) ||
				number > MAX_CD_TRACK || number == 1 ||
				number != (unsigned int)number)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate CDTrack");

			parsed.cd_track = (unsigned int)number;
			cd_seen = 1;
			continue;
		}

		if (strcmp(key, "ai1") == 0 || strcmp(key, "ai2") == 0 ||
			strcmp(key, "ai3") == 0) {
			unsigned int difficulty;

			difficulty = (unsigned int)(key[2] - '1');

			if (parsed.has_ai[difficulty] ||
				!read_values(line + key_end,
					parsed.ai[difficulty],
					MH_TRACK_AI_VALUES)) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate AI difficulty (expected eleven values in 0..100)");
			}

			for (i = 0; i < MH_TRACK_AI_VALUES; i++)
				parsed.ai[difficulty][i] =
					(double)(float)parsed.ai[difficulty][i];

			parsed.has_ai[difficulty] = 1;
			continue;
		}

		destination = path_field(key, &parsed);

		if (destination != NULL) {
			path_end = 0;

			if (destination[0] != 0 ||
				sscanf(line + key_end, "%255s %n", path,
					&path_end) != 1 ||
				line[key_end + path_end] != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate track path");

			strcpy(destination, path);
			continue;
		}

		if (strcmp(key, "envmapname") != 0)
			continue;

		if (environment_count == MH_TRACK_ENVIRONMENTS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many EnvMapName entries");

		path_end = 0;

		if (sscanf(line + key_end, "%255s %n", path, &path_end) != 1 ||
			line[key_end + path_end] != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid EnvMapName path");

		strcpy(parsed.environment[environment_count++], path);
	}

	if (environment_count != MH_TRACK_ENVIRONMENTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected three EnvMapName entries");

	*track = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
