#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "client/settings/preferences.h"
#include "client/settings/render_choice.h"
#include "engine/audio/horn.h"
#include "engine/audio/music_catalog.h"
#include "engine/catalog/catalog.h"
#include "engine/core/text.h"
#include "engine/game/race_limits.h"
#include "engine/render/viewport.h"
#include "engine/ui/menu_graphics.h"

#define FIELD(key, member, type, low, high, step, choices, feature, row, base, \
	volume, detail)                                                        \
	{key, MH_PREFERENCE_##type, offsetof(struct mh_settings, member),      \
		sizeof(((struct mh_settings *)0)->member), low, high, step,    \
		choices, MH_PREFERENCE_##feature, row, base, volume, detail}
static const struct mh_preference fields[] = {
	FIELD("player.name", player_name, TEXT, 1, 15, 0, "", COMMON, 0, 0, 0,
		0),
	FIELD("player.team", team_name, TEXT, 0, 4, 0, "", COMMON, 0, 0, 0, 0),
	FIELD("player.color", player_color, ULONG, 0, 0xffffff, 1, "", COMMON,
		0, 0, 0, 0),
	FIELD("player.units", av.system, UINT, 0, 1, 1, "0:Metric|1:Imperial",
		COMMON, 0, 0, 0, 0),
	FIELD("player.customCarColors", custom_car_colors, UINT, 0, 1, 1, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.carColor1", car_colors[0], ULONG, 0, 0xffffff, 1, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.carColor2", car_colors[1], ULONG, 0, 0xffffff, 1, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.carColor3", car_colors[2], ULONG, 0, 0xffffff, 1, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut0", short_keys[0], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut1", short_keys[1], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut2", short_keys[2], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut3", short_keys[3], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut4", short_keys[4], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut5", short_keys[5], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut6", short_keys[6], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut7", short_keys[7], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut8", short_keys[8], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("player.chatShortcut9", short_keys[9], TEXT, 0, 127, 0, "",
		COMMON, 0, 0, 0, 0),
	FIELD("car", car, UINT, 1, MH_CAR_CATALOG_LIMIT, 1, "", COMMON, 0, 1, 0,
		0),
	FIELD("track", track, UINT, 1, MH_TRACK_CATALOG_LIMIT, 1, "", COMMON, 0,
		1, 0, 0),
	FIELD("controls.profile", control_name, TEXT, 0, 127, 0, "", COMMON, 0,
		0, 0, 0),
	FIELD("horn", horn, UINT, 0, MH_HORNS - 1, 1, "", COMMON, 0, 0, 0, 0),
	FIELD("mode", game_type, UINT, 0, 3, 1,
		"0:Quick race|1:League|2:Time attack|3:Ghost", COMMON, 0, 0, 0,
		0),
	FIELD("laps", laps, UINT, MH_RACE_MIN_LAPS, MH_RACE_MAX_LAPS, 1, "",
		COMMON, 0, 0, 0, 0),
	FIELD("difficulty", difficulty, UINT, 0, 2, 1, "0:Easy|1:Medium|2:Hard",
		COMMON, 0, 0, 0, 0),
	FIELD("automatic", automatic, UINT, 0, 1, 1, "", COMMON, 0, 0, 0, 0),
	FIELD("catchUp", catch_up, UINT, 0, 1, 1, "", COMMON, 0, 0, 0, 0),
	FIELD("camera", av.car_view, UINT, 0, 1, 1, "0:Inside|1:Outside",
		COMMON, 0, 0, 0, 0),
	FIELD("recordRace", record_race, UINT, 0, 1, 1, "", RECORDING, 0, 0, 0,
		0),
	FIELD("league", league_name, TEXT, 0, 15, 0, "", MENUS, 0, 0, 0, 0),
	FIELD("recording", demo_filename, TEXT, 0, 63, 0, "", RECORDING, 0, 0,
		0, 0),
	FIELD("menu.confirmation", menu_confirmation, UINT, 0, 2, 1,
		"0:Off|1:On|2:No exit", MENUS, 0, 0, 0, 0),
	FIELD("graphics.renderer", renderer, UINT, 0,
		MH_RENDER_CHOICE_COUNT - 1, 1, "", COMMON, 0, 0, 0, 0),
	FIELD("graphics.width", av.width, ULONG, MH_VIEW_MIN_WIDTH,
		MH_VIEW_MAX_WIDTH, 1, "", COMMON, 0, 0, 0, 0),
	FIELD("graphics.height", av.height, ULONG, MH_VIEW_MIN_HEIGHT,
		MH_VIEW_MAX_HEIGHT, 1, "", COMMON, 0, 0, 0, 0),
	FIELD("graphics.detail", av.detail_mode, UINT, 0, MH_DETAIL_CUSTOM, 1,
		"0:Low|1:Medium|2:High|3:Max|4:Custom", COMMON, 0, 0, 0, 0),
	FIELD("graphics.showFPS", av.show_fps, UINT, 0, 1, 1, "", COMMON, 0, 0,
		0, 0),
	FIELD("graphics.brightnessRed", av.brightness[0], FLOAT, 0,
		MH_AV_BRIGHTNESS_MAX, 0, "", COMMON, 0, 0, 0, 0),
	FIELD("graphics.brightnessGreen", av.brightness[1], FLOAT, 0,
		MH_AV_BRIGHTNESS_MAX, 0, "", COMMON, 0, 0, 0, 0),
	FIELD("graphics.brightnessBlue", av.brightness[2], FLOAT, 0,
		MH_AV_BRIGHTNESS_MAX, 0, "", COMMON, 0, 0, 0, 0),
	FIELD("graphics.infoDetail", av.info_detail, UINT, 0, MH_AV_INFO_ALL, 1,
		"0:None|1:Selective|2:All", COMMON, 0, 0, 0, 0),
	FIELD("graphics.checkpointInfo", av.checkpoint_info, UINT, 0,
		MH_AV_INFO_ALL, 1, "0:None|1:Selective|2:All", COMMON, 0, 0, 0,
		0),
	FIELD("graphics.roadMap", av.road_map, UINT, 0, MH_AV_INFO_ALL, 1,
		"0:None|1:Selective|2:All", COMMON, 0, 0, 0, 0),
	FIELD("graphics.checkpointDelay", av.checkpoint_delay, ULONG, 0,
		MH_AV_CHECKPOINT_DELAY_MAX, 1, "", COMMON, 0, 0, 0, 0),
	FIELD("graphics.lensFlares", av.lens_flares, UINT, 0, 1, 1, "", COMMON,
		0, 0, 0, 1),
	FIELD("graphics.sparks", av.sparks, UINT, 0, 1, 1, "", COMMON, 0, 0, 0,
		1),
	FIELD("graphics.smoke", av.smoke, UINT, 0, 1, 1, "", COMMON, 0, 0, 0,
		1),
	FIELD("graphics.halos", av.halos, UINT, 0, 1, 1, "", COMMON, 0, 0, 0,
		1),
	FIELD("graphics.shadow", av.shadow, UINT, 0, 1, 1, "", COMMON, 0, 0, 0,
		1),
	FIELD("graphics.skidMarks", av.skid_marks, UINT, 0, 1, 1, "", COMMON, 0,
		0, 0, 1),
	FIELD("graphics.sky", av.sky_image, UINT, 0, 1, 1, "", COMMON, 0, 0, 0,
		1),
	FIELD("graphics.namePlates", av.name_plates, UINT, 0, 2, 1,
		"0:None|1:Flat|2:Transparent", COMMON, 0, 0, 0, 1),
	FIELD("graphics.trackDetail", av.track_detail, UINT, 0, 1, 1,
		"0:Medium|1:High", COMMON, 0, 0, 0, 1),
	FIELD("graphics.carDetail", av.car_detail, UINT, 0, 2, 1,
		"0:Low|1:Medium|2:High", COMMON, 0, 0, 0, 1),
	FIELD("graphics.carShading", av.car_shading, UINT, 0, 3, 1,
		"0:Flat|1:Gouraud|2:Reflection|3:Glenz", COMMON, 0, 0, 0, 1),
	FIELD("graphics.perspective", av.perspective_on, UINT, 0, 1, 1,
		"0:Selective|1:All", COMMON, 0, 0, 0, 1),
	FIELD("graphics.perspectiveQuality", av.perspective, UINT, 16, 32, 16,
		"16:Good|32:Normal", COMMON, 0, 0, 0, 1),
	FIELD("graphics.viewDistance", av.view_distance, FLOAT,
		MH_AV_VIEW_DISTANCE_MIN, MH_AV_VIEW_DISTANCE_MAX, 0, "", COMMON,
		0, 0, 0, 1),
	FIELD("graphics.texturePrecision", av.texture_precision, UINT, 0,
		MH_AV_TEXTURE_32BIT, 1, "0:16 bit|1:32 bit", GRAPHICS,
		MH_MENU_GRAPHICS_TEXTURE_PRECISION, 0, 0, 1),
	FIELD("graphics.textureFilter", av.texture_filter, UINT, 0,
		MH_AV_FILTER_TRILINEAR, 1,
		"0:Original|1:Nearest|2:Bilinear|3:Trilinear", GRAPHICS,
		MH_MENU_GRAPHICS_TEXTURE_FILTER, 0, 0, 1),
	FIELD("graphics.frameQueue", av.frame_queue, UINT, 0,
		MH_AV_FRAME_QUEUE_THREE, 1, "0:One|1:Two|2:Three", GRAPHICS,
		MH_MENU_GRAPHICS_FRAME_QUEUE, 0, 0, 1),
	FIELD("graphics.motionBlur", av.motion_blur, UINT, 0, 1, 1, "",
		GRAPHICS, MH_MENU_GRAPHICS_MOTION_BLUR, 0, 0, 1),
	FIELD("graphics.tripleBuffer", av.triple_buffer, UINT, 0, 1, 1, "",
		GRAPHICS, MH_MENU_GRAPHICS_TRIPLE_BUFFER, 0, 0, 1),
	FIELD("graphics.fullscreen", av.fullscreen, UINT, 0, 1, 1, "", WINDOW,
		0, 0, 0, 0),
	FIELD("graphics.grabMouse", av.grab_mouse, UINT, 0, 1, 1, "", WINDOW, 0,
		0, 0, 0),
	FIELD("graphics.vsync", av.vsync, UINT, 0, 1, 1, "", PRESENTATION, 0, 0,
		0, 0),
	FIELD("graphics.maxFPS", av.max_fps, ULONG, 0, MH_AV_MAX_FPS, 1, "",
		PRESENTATION, 0, 0, 0, 0),
	FIELD("graphics.screenshotFormat", av.screenshot_format, UINT, 0,
		MH_SCREENSHOT_FORMAT_COUNT - 1, 1, "0:PNG|1:TGA", SCREENSHOT, 0,
		0, 0, 0),
	FIELD("sound.effectsVolume", av.effects_volume, UINT, 0, 100, 1, "",
		COMMON, 0, 0, 1, 0),
	FIELD("sound.musicVolume", av.music_volume, UINT, 0, 100, 1, "", COMMON,
		0, 0, 1, 0),
	FIELD("sound.profile", av.race_audio, UINT, 0, MH_RACE_AUDIO_A3D20, 1,
		"", COMMON, 0, 0, 0, 0),
	FIELD("sound.loop", av.cd_loop, UINT, 0, 1, 1, "", COMMON, 0, 0, 0, 0),
	FIELD("sound.menuTrack", menu_cd_track, ULONG, 2,
		MH_MUSIC_CATALOG_LAST_TRACK, 1, "", MENUS, 0, 0, 0, 0),
	FIELD("network.rate", tcp_rate, UINT, MH_SETTINGS_RATE_MIN,
		MH_SETTINGS_RATE_MAX, 1, "", NETWORK, 0, 0, 0, 0),
	FIELD("network.predictionMin", prediction.minimum, UINT, 0,
		MH_NET_PREDICTION_LIMIT, 1, "", NETWORK, 0, 0, 0, 0),
	FIELD("network.predictionMax", prediction.maximum, UINT, 0,
		MH_NET_PREDICTION_LIMIT, 1, "", NETWORK, 0, 0, 0, 0),
	FIELD("network.protocol", protocol, UINT, 0, 3, 1, "", LEGACY_NETWORK,
		0, 0, 0, 0),
	FIELD("network.serialPort", serial_port, UINT, 1, 4, 1, "",
		LEGACY_NETWORK, 0, 0, 0, 0),
	FIELD("network.baudRate", baud_rate, UINT, 0, 5, 1, "", LEGACY_NETWORK,
		0, 0, 0, 0),
	FIELD("network.parity", parity, UINT, 0, 3, 1, "", LEGACY_NETWORK, 0, 0,
		0, 0),
	FIELD("network.stopBits", stop_bits, UINT, 0, 2, 1, "", LEGACY_NETWORK,
		0, 0, 0, 0),
	FIELD("network.flowControl", flow_control, UINT, 0, 4, 1, "",
		LEGACY_NETWORK, 0, 0, 0, 0),
	FIELD("network.sessionName", session_name, TEXT, 0, 15, 0, "",
		LEGACY_NETWORK, 0, 0, 0, 0),
	FIELD("network.password", session_password, TEXT, 0, 15, 0, "",
		LEGACY_NETWORK, 0, 0, 0, 0),
	FIELD("network.hiscorePath", hiscore_path, TEXT, 0, 511, 0, "",
		LEGACY_NETWORK, 0, 0, 0, 0)};
#undef FIELD

unsigned int
mh_preference_count(unsigned int courses)
{
	return (unsigned int)(sizeof(fields) / sizeof(fields[0])) +
		(courses <= MH_SETTINGS_COURSES ? courses : 0);
}

int
mh_preference_describe(
	unsigned int index, unsigned int courses, struct mh_preference *output)
{
	unsigned int fixed;

	fixed = (unsigned int)(sizeof(fields) / sizeof(fields[0]));

	if (output == NULL || index >= mh_preference_count(courses))
		return 0;

	if (index < fixed) {
		*output = fields[index];
		return 1;
	}

	index -= fixed;
	memset(output, 0, sizeof(*output));
	sprintf(output->name, "sound.courseMusic.%u", index + 1);
	output->type = MH_PREFERENCE_ULONG;
	output->offset = offsetof(struct mh_settings, course_music) +
		index * sizeof(struct mh_settings_course_music) +
		offsetof(struct mh_settings_course_music, track);
	output->minimum = 2;
	output->maximum = MH_MUSIC_CATALOG_LAST_TRACK;
	output->step = 1;
	output->choices = "";
	output->feature = MH_PREFERENCE_MUSIC;

	return 1;
}

double
mh_preference_number(
	const struct mh_settings *settings, const struct mh_preference *field)
{
	const char *pointer;
	double value;

	pointer = (const char *)settings + field->offset;

	switch (field->type) {
	case MH_PREFERENCE_UINT:
		value = *(const unsigned int *)pointer;
		break;

	case MH_PREFERENCE_ULONG:
		value = *(const unsigned long *)pointer;
		break;

	case MH_PREFERENCE_FLOAT:
		value = *(const float *)pointer;
		break;

	default:
		return 0;
	}

	if (field->volume)
		value = floor((value * 100 + 127) / MH_AV_VOLUME_MAX);

	return value + field->one_based;
}

const char *
mh_preference_text(
	const struct mh_settings *settings, const struct mh_preference *field)
{
	return field->type == MH_PREFERENCE_TEXT
		? (const char *)settings + field->offset
		: NULL;
}

static int
valid_text(const struct mh_preference *field, const char *text)
{
	size_t i;

	if (text == NULL)
		return 0;

	for (i = 0; i < field->capacity && text[i] != 0; i++) {
		/*
		 * Prevent CFG comment/line injection and lossy whitespace
		 * parsing.
		 */
		if ((unsigned char)text[i] < 32 ||
			(unsigned char)text[i] > 126 || text[i] == ';' ||
			(i != 0 && text[i] == '/' && text[i - 1] == '/') ||
			(i != 0 && text[i] == ' ' && text[i - 1] == ' ') ||
			(field->offset ==
					offsetof(struct mh_settings,
						control_name) &&
				text[i] == ' '))
			return 0;
	}

	return i >= field->minimum && i <= field->maximum &&
		(i == 0 || (text[0] != ' ' && text[i - 1] != ' '));
}

enum mh_error_code
mh_preference_set(struct mh_settings *settings,
	const struct mh_preference *field, double number, const char *text,
	struct mh_error *error_out)
{
	char *pointer;
	double value;

	if (settings == NULL || field == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing preference");

	pointer = (char *)settings + field->offset;

	if (field->type == MH_PREFERENCE_TEXT) {
		if (!valid_text(field, text))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"preference text cannot round-trip through the saved configuration");

		return mh_text_copy(pointer, field->capacity, text, error_out);
	}

	/* Ordered comparisons reject NaN and infinities before any cast. */
	if (!(number >= field->minimum && number <= field->maximum) ||
		(field->step != 0 &&
			fmod(number - field->minimum, field->step) != 0))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"preference is outside its allowed range");

	value = number - field->one_based;

	if (field->volume)
		value = floor((value * MH_AV_VOLUME_MAX + 50) / 100);

	switch (field->type) {
	case MH_PREFERENCE_UINT:
		*(unsigned int *)pointer = (unsigned int)value;
		break;

	case MH_PREFERENCE_ULONG:
		*(unsigned long *)pointer = (unsigned long)value;
		break;

	case MH_PREFERENCE_FLOAT:
		*(float *)pointer = (float)value;
		break;

	default:
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid preference type");
	}

	if (field->custom_detail)
		settings->av.detail_mode = MH_DETAIL_CUSTOM;

	if (field->offset == offsetof(struct mh_settings, av.detail_mode))
		return mh_av_settings_preset(&settings->av, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
