#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/settings/av.h"
#include "engine/settings/av_internal.h"
#include "engine/settings/parse.h"

enum mh_error_code
mh_av_settings_preset(
	struct mh_av_settings *settings, struct mh_error *error_out)
{
	static const float preset_distance[MH_DETAIL_CUSTOM] = {
		50, 60, 80, 100};

	unsigned int mode;

	if (settings == NULL || settings->detail_mode > MH_DETAIL_CUSTOM)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid graphics detail mode");

	mode = settings->detail_mode;

	if (mode == MH_DETAIL_CUSTOM)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	settings->lens_flares = settings->halos = settings->skid_marks =
		settings->sky_image = 1;
	settings->sparks = settings->shadow = settings->track_detail =
		mode != MH_DETAIL_LOW;
	settings->smoke = mode >= MH_DETAIL_HIGH;
	settings->name_plates = mode >= MH_DETAIL_HIGH ? MH_AV_NAMES_TRANSPARENT
						: MH_AV_NAMES_FLAT;
	settings->car_detail = mode >= MH_DETAIL_HIGH ? MH_DETAIL_HIGH : mode;
	settings->car_shading = mode == MH_DETAIL_LOW
		? MH_AV_SHADING_FLAT
		: MH_AV_SHADING_REFLECTION;
	settings->perspective_on = mode == MH_DETAIL_MAX;
	settings->perspective = mode == MH_DETAIL_MAX
		? MH_AV_PERSPECTIVE_GOOD
		: MH_AV_PERSPECTIVE_NORMAL;
	settings->motion_blur = 0;
	settings->view_distance = preset_distance[mode];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_av_settings_init(struct mh_av_settings *settings,
	unsigned int effects_volume, unsigned int music_volume,
	struct mh_error *error_out)
{
	if (settings == NULL || effects_volume > MH_AV_VOLUME_MAX ||
		music_volume > MH_AV_VOLUME_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid settings initialization");

	memset(settings, 0, sizeof(*settings));
	settings->grab_mouse = 1;
	settings->vsync = 1;
	settings->screenshot_format = MH_SCREENSHOT_PNG;
	settings->width = 320;
	settings->height = 200;
	settings->brightness[0] = settings->brightness[1] =
		settings->brightness[2] = 1;
	settings->info_detail = settings->checkpoint_info = settings->road_map =
		MH_AV_INFO_ALL;
	settings->checkpoint_delay = 4000;
	settings->detail_mode = MH_DETAIL_HIGH;
	settings->frame_queue = MH_AV_FRAME_QUEUE_TWO;
	settings->texture_precision = MH_AV_TEXTURE_32BIT;
	settings->effects_volume = effects_volume;
	settings->music_volume = music_volume;
	settings->cd_loop = 1;

	return mh_av_settings_preset(settings, error_out);
}

enum mh_error_code
mh_cfg_av_line(char **tokens, unsigned int count, void *context,
	struct mh_error *error_out)
{
	static const char *const detail[] = {
		"Low", "Medium", "High", "Max", "Custom"};

	static const char *const screenshot_format[] = {"PNG", "TGA"};

	static const char *const car_view[] = {"Inside", "Outside"};

	static const char *const info[] = {"None", "Selective", "All"};
	static const char *const names[] = {"None", "Flat", "Transparent"};
	static const char *const shading[] = {
		"Flat", "Gouraud", "Reflection", "Glenz"};

	static const char *const frame_queue[] = {"1", "2", "3"};
	static const char *const texture_precision[] = {"16", "32"};
	static const char *const texture_filter[] = {
		"Original", "Nearest", "Bilinear", "Trilinear"};

	static const char *const correction[] = {"Selective", "All"};
	static const char *const quality[] = {"Normal", "Good"};
	static const char *const loop[] = {"Off", "On"};
	static const char *const race_audio[] = {
		"Software", "DS3D", "EAX", "A3D12", "A3D20"};

	static const char *const units[] = {"Metric", "Imperial"};
	struct mh_av_settings *settings;
	const char *key, *value;
	double numeric;
	long double scaled;
	unsigned long whole;
	unsigned int i, index;
	float brightness;

	if (count < 2)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	settings = (struct mh_av_settings *)context;
	key = tokens[0];
	value = tokens[1];

	if (mh_ascii_equal(key, "MaxFPS")) {
		if (!mh_cfg_number(value, &numeric) || numeric < 0 ||
			numeric > MH_AV_MAX_FPS ||
			!mh_cfg_integer(numeric, &whole) || numeric != whole)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid race FPS limit");

		settings->max_fps = whole;
	} else if (mh_ascii_equal(key, "ScreenDimension")) {
		for (i = 1; i < count && i < 3; i++) {
			if (!mh_cfg_number(tokens[i], &numeric) ||
				!mh_cfg_integer(numeric, &whole))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid screen dimension");

			if (i == 1)
				settings->width = whole;
			else
				settings->height = whole;
		}
	} else if (mh_ascii_equal(key, "Brightness")) {
		for (i = 1; i < count && i <= MH_AV_COLOR_CHANNELS; i++) {
			if (!mh_cfg_number(tokens[i], &numeric))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid brightness");

			scaled = (long double)numeric * (double).01;

			if (scaled < -FLT_MAX)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"brightness outside float range");

			brightness = scaled > MH_AV_BRIGHTNESS_MAX
				? MH_AV_BRIGHTNESS_MAX
				: (float)scaled;
			settings->brightness[i - 1] = brightness;
		}
	} else if (mh_ascii_equal(key, "CheckpointDelay") ||
		mh_ascii_equal(key, "SFXVolume") ||
		mh_ascii_equal(key, "MusicVolume")) {
		if (!mh_cfg_number(value, &numeric))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid settings number");

		scaled = (long double)numeric *
			(mh_ascii_equal(key, "CheckpointDelay") ? 1000.0
								: 2.55);

		if (!mh_cfg_integer(scaled, &whole))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"settings integer outside supported range");

		if (mh_ascii_equal(key, "CheckpointDelay"))
			settings->checkpoint_delay =
				whole > MH_AV_CHECKPOINT_DELAY_MAX
				? MH_AV_CHECKPOINT_DELAY_MAX
				: whole;
		else if (mh_ascii_equal(key, "SFXVolume"))
			settings->effects_volume = whole > MH_AV_VOLUME_MAX
				? MH_AV_VOLUME_MAX
				: (unsigned int)whole;
		else
			settings->music_volume = whole > MH_AV_VOLUME_MAX
				? MH_AV_VOLUME_MAX
				: (unsigned int)whole;
	} else if (mh_ascii_equal(key, "ViewDistance")) {
		if (!mh_cfg_number(value, &numeric))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid view distance");

		if (numeric < MH_AV_VIEW_DISTANCE_MIN)
			settings->view_distance = MH_AV_VIEW_DISTANCE_MIN;
		else if (numeric > MH_AV_VIEW_DISTANCE_MAX)
			settings->view_distance = MH_AV_VIEW_DISTANCE_MAX;
		else
			settings->view_distance = (float)numeric;
	} else if (mh_ascii_equal(key, "System"))
		mh_cfg_choice(value, units, sizeof(units) / sizeof(units[0]),
			&settings->system);
	else if (mh_ascii_equal(key, "CarView"))
		mh_cfg_choice(value, car_view,
			sizeof(car_view) / sizeof(car_view[0]),
			&settings->car_view);
	else if (mh_ascii_equal(key, "InfoDetail"))
		mh_cfg_choice(value, info, sizeof(info) / sizeof(info[0]),
			&settings->info_detail);
	else if (mh_ascii_equal(key, "CheckpointInfo"))
		mh_cfg_choice(value, info, sizeof(info) / sizeof(info[0]),
			&settings->checkpoint_info);
	else if (mh_ascii_equal(key, "RoadMap"))
		mh_cfg_choice(value, info, sizeof(info) / sizeof(info[0]),
			&settings->road_map);
	else if (mh_ascii_equal(key, "DetailMode"))
		mh_cfg_choice(value, detail, sizeof(detail) / sizeof(detail[0]),
			&settings->detail_mode);
	else if (mh_ascii_equal(key, "NamePlates"))
		mh_cfg_choice(value, names, sizeof(names) / sizeof(names[0]),
			&settings->name_plates);
	else if (mh_ascii_equal(key, "TrackDetail"))
		mh_cfg_choice(value, detail + MH_DETAIL_MEDIUM,
			MH_DETAIL_HIGH - MH_DETAIL_MEDIUM + 1,
			&settings->track_detail);
	else if (mh_ascii_equal(key, "CarDetail"))
		mh_cfg_choice(value, detail, MH_DETAIL_HIGH + 1,
			&settings->car_detail);
	else if (mh_ascii_equal(key, "CarShading"))
		mh_cfg_choice(value, shading,
			sizeof(shading) / sizeof(shading[0]),
			&settings->car_shading);
	else if (mh_ascii_equal(key, "TrippleBuffer"))
		mh_cfg_choice(value, loop, sizeof(loop) / sizeof(loop[0]),
			&settings->triple_buffer);
	else if (mh_ascii_equal(key, "FrameQueue"))
		mh_cfg_choice(value, frame_queue,
			sizeof(frame_queue) / sizeof(frame_queue[0]),
			&settings->frame_queue);
	else if (mh_ascii_equal(key, "TextureFiltering"))
		mh_cfg_choice(value, texture_filter,
			sizeof(texture_filter) / sizeof(texture_filter[0]),
			&settings->texture_filter);
	else if (mh_ascii_equal(key, "TexturePrecision") ||
		mh_ascii_equal(key, "GlenzMapPrecision"))
		mh_cfg_choice(value, texture_precision,
			sizeof(texture_precision) /
				sizeof(texture_precision[0]),
			&settings->texture_precision);
	else if (mh_ascii_equal(key, "PerspectiveOn"))
		mh_cfg_choice(value, correction,
			sizeof(correction) / sizeof(correction[0]),
			&settings->perspective_on);
	else if (mh_ascii_equal(key, "Perspective")) {
		index = settings->perspective == MH_AV_PERSPECTIVE_GOOD ? 1 : 0;
		mh_cfg_choice(value, quality,
			sizeof(quality) / sizeof(quality[0]), &index);
		settings->perspective = index == 1 ? MH_AV_PERSPECTIVE_GOOD
						: MH_AV_PERSPECTIVE_NORMAL;
	} else if (mh_ascii_equal(key, "CDLoop"))
		mh_cfg_choice(value, loop, sizeof(loop) / sizeof(loop[0]),
			&settings->cd_loop);
	else if (mh_ascii_equal(key, "RaceAudio"))
		mh_cfg_choice(value, race_audio,
			sizeof(race_audio) / sizeof(race_audio[0]),
			&settings->race_audio);
	else if (mh_ascii_equal(key, "SpatialAudio"))
		mh_cfg_choice(value, loop, sizeof(loop) / sizeof(loop[0]),
			&settings->race_audio);
	else if (mh_ascii_equal(key, "LensFlares"))
		settings->lens_flares = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "Sparks"))
		settings->sparks = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "Smoke"))
		settings->smoke = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "Halos"))
		settings->halos = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "Shadow"))
		settings->shadow = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "SkidMarks"))
		settings->skid_marks = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "SkyImage"))
		settings->sky_image = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "ScreenshotFormat"))
		mh_cfg_choice(value, screenshot_format,
			MH_SCREENSHOT_FORMAT_COUNT,
			&settings->screenshot_format);
	else if (mh_ascii_equal(key, "VSync"))
		settings->vsync = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "ShowFPS"))
		settings->show_fps = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "GrabMouse"))
		settings->grab_mouse = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "Fullscreen"))
		settings->fullscreen = mh_cfg_boolean(value);
	else if (mh_ascii_equal(key, "MotionBlur"))
		settings->motion_blur = mh_cfg_boolean(value);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_av_settings_parse(const unsigned char *text, size_t size,
	struct mh_av_settings *settings, struct mh_error *error_out)
{
	struct mh_av_settings next;
	enum mh_error_code status;

	if (settings == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing settings");

	next = *settings;
	status = mh_cfg_parse(text, size, mh_cfg_av_line, &next, error_out);

	if (status == MH_ERROR_NONE)
		*settings = next;

	return status;
}
