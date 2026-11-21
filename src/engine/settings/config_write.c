#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/game/mode.h"
#include "engine/game/race_limits.h"
#include "engine/settings/config_internal.h"
#include "engine/settings/decimal.h"

struct config_writer {
	char		text[MH_SETTINGS_TEXT_CAPACITY];
	size_t		size;
	struct mh_error	error;
};

static void
append_text(struct config_writer *writer, const char *text)
{
	if (writer->error.code != MH_ERROR_NONE)
		return;

	if (mh_text_copy(writer->text + writer->size,
		sizeof(writer->text) - writer->size, text,
		NULL) != MH_ERROR_NONE) {
		mh_error_set(&writer->error, MH_ERROR_RANGE,
			"settings output too large");
		return;
	}

	writer->size += strlen(writer->text + writer->size);
}

static int
line_text_valid(const char *text, size_t capacity)
{
	size_t i;

	for (i = 0; i < capacity; i++) {
		if (text[i] == 0)
			return 1;

		if ((unsigned char)text[i] < 32)
			return 0;
	}

	return 0;
}

static int
value_text_valid(const char *text, size_t capacity)
{
	size_t i;

	if (!line_text_valid(text, capacity))
		return 0;

	for (i = 0; text[i] != 0; i++)
		if (text[i] == ';' || (text[i] == '/' && text[i + 1] == '/'))
			return 0;

	return 1;
}

static void
append_line(struct config_writer *writer, const char *key, const char *text)
{
	append_text(writer, key);
	append_text(writer, "\t");
	append_text(writer, text);
	append_text(writer, "\n");
}

static void
append_integer(struct config_writer *writer, unsigned long value)
{
	char text[16];
	long signed_value;

	/*
	 * Save signed 32-bit values. Avoid implementation-defined unsigned to
	 * signed conversion, including the LONG_MIN case on a 32-bit host.
	 */
	signed_value = value <= (unsigned long)MH_I32_MAX
		? (long)value
		: -1L - (long)(MH_U32_MAX - value);
	sprintf(text, "%ld", signed_value);
	append_text(writer, text);
}

static void
append_integer_line(
	struct config_writer *writer, const char *key, unsigned long value)
{
	append_text(writer, key);

	append_text(writer, "\t");
	append_integer(writer, value);
	append_text(writer, "\n");
}

static void
append_color(struct config_writer *writer, const char *key, unsigned long value)
{
	append_text(writer, key);

	append_text(writer, "\t");
	append_integer(writer, (value >> 16) & MH_CFG_COLOR_BYTE);
	append_text(writer, " ");
	append_integer(writer, (value >> 8) & MH_CFG_COLOR_BYTE);
	append_text(writer, " ");
	append_integer(writer, value & MH_CFG_COLOR_BYTE);
	append_text(writer, "\n");
}

static void
append_decimal(struct config_writer *writer, float value)
{
	char text[MH_SETTINGS_DECIMAL_BYTES];
	enum mh_error_code status;

	if (writer->error.code != MH_ERROR_NONE)
		return;

	status = mh_settings_decimal(value, 2, text, &writer->error);

	if (status == MH_ERROR_NONE)
		append_text(writer, text);
}

static void
append_decimal_line(struct config_writer *writer, const char *key, float value)
{
	append_text(writer, key);
	append_text(writer, "\t");
	append_decimal(writer, value);
	append_text(writer, "\n");
}

static enum mh_error_code
validate_av(const struct mh_av_settings *av, struct mh_error *error_out)
{
	unsigned int i;

	if (av->width > MH_U32_MAX || av->height > MH_U32_MAX ||
		av->system > MH_UNITS_IMPERIAL ||
		av->car_view > MH_CAR_VIEW_OUTSIDE ||
		av->info_detail > MH_AV_INFO_ALL ||
		av->checkpoint_info > MH_AV_INFO_ALL ||
		av->road_map > MH_AV_INFO_ALL ||
		av->checkpoint_delay > MH_AV_CHECKPOINT_DELAY_MAX ||
		av->detail_mode > MH_DETAIL_CUSTOM || av->lens_flares > 1 ||
		av->sparks > 1 || av->smoke > 1 || av->halos > 1 ||
		av->skid_marks > 1 || av->shadow > 1 ||
		av->name_plates > MH_AV_NAMES_TRANSPARENT ||
		av->sky_image > 1 || av->track_detail > 1 ||
		av->car_detail > MH_DETAIL_HIGH ||
		av->car_shading > MH_AV_SHADING_GLENZ ||
		av->texture_precision > MH_AV_TEXTURE_32BIT ||
		av->texture_filter > MH_AV_FILTER_TRILINEAR ||
		av->frame_queue > MH_AV_FRAME_QUEUE_THREE ||
		av->perspective_on > 1 ||
		(av->perspective != MH_AV_PERSPECTIVE_GOOD &&
			av->perspective != MH_AV_PERSPECTIVE_NORMAL) ||
		av->vsync > 1 || av->max_fps > MH_AV_MAX_FPS ||
		av->screenshot_format >= MH_SCREENSHOT_FORMAT_COUNT ||
		av->show_fps > 1 || av->fullscreen > 1 || av->grab_mouse > 1 ||
		av->motion_blur > 1 || av->triple_buffer > 1 ||
		!(av->view_distance >= MH_AV_VIEW_DISTANCE_MIN &&
			av->view_distance <= MH_AV_VIEW_DISTANCE_MAX) ||
		av->effects_volume > MH_AV_VOLUME_MAX ||
		av->music_volume > MH_AV_VOLUME_MAX || av->cd_loop > 1 ||
		av->race_audio > MH_RACE_AUDIO_A3D20)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audiovisual settings value");

	for (i = 0; i < MH_AV_COLOR_CHANNELS; i++)
		if (!(av->brightness[i] >= -(long double)FLT_MAX / 100 &&
			av->brightness[i] <= MH_AV_BRIGHTNESS_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"brightness cannot be serialized as a finite percentage");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_settings(const struct mh_settings *settings,
	const struct mh_settings_catalog *catalog, struct mh_error *error_out)
{
	unsigned int i;

	if (!mh_cfg_catalog_valid(catalog, settings))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid settings catalog or state");

	/*
	 * Personal accepts printable comment punctuation. The original writes
	 * it literally; a later CFG load has the original lossy lexer behavior.
	 * Preserve that behavior without allowing control bytes/new lines.
	 */
	if (!line_text_valid(
		settings->player_name, sizeof(settings->player_name)) ||
		!line_text_valid(
			settings->team_name, sizeof(settings->team_name)) ||
		!value_text_valid(settings->control_name,
			sizeof(settings->control_name)) ||
		!value_text_valid(
			settings->league_name, sizeof(settings->league_name)) ||
		!value_text_valid(settings->demo_filename,
			sizeof(settings->demo_filename)) ||
		!value_text_valid(settings->session_name,
			sizeof(settings->session_name)) ||
		!value_text_valid(settings->session_password,
			sizeof(settings->session_password)) ||
		!value_text_valid(
			settings->hiscore_path, sizeof(settings->hiscore_path)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"settings text cannot be represented in CFG");

	for (i = 0; i < MH_SETTINGS_SHORTCUTS; i++)
		if (!line_text_valid(settings->short_keys[i],
			sizeof(settings->short_keys[i])))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid settings short key");

	if (!value_text_valid(catalog->cars.names[settings->car],
		MH_SETTINGS_CATALOG_NAME_BYTES) ||
		!value_text_valid(catalog->tracks.names[settings->track],
			MH_SETTINGS_CATALOG_NAME_BYTES) ||
		!value_text_valid(catalog->horns.names[settings->horn],
			MH_SETTINGS_HORN_NAME_BYTES) ||
		(settings->renderer != MH_SETTINGS_NO_RENDERER &&
			!value_text_valid(
				catalog->renderers.names[settings->renderer],
				MH_SETTINGS_CATALOG_NAME_BYTES)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid settings catalog text");

	if (settings->player_color > MH_CFG_RGB_MAX ||
		settings->car_colors[0] > MH_CFG_RGB_MAX ||
		settings->car_colors[1] > MH_CFG_RGB_MAX ||
		settings->car_colors[2] > MH_CFG_RGB_MAX ||
		settings->custom_car_colors > 1 || settings->automatic > 1 ||
		settings->game_type > MH_GAME_GHOST ||
		settings->difficulty > 2 || settings->catch_up > 1 ||
		settings->record_race > 1 || settings->protocol > 3 ||
		settings->tcp_rate < MH_SETTINGS_RATE_MIN ||
		settings->tcp_rate > MH_SETTINGS_RATE_MAX ||
		settings->prediction.minimum > MH_NET_PREDICTION_LIMIT ||
		settings->prediction.maximum > MH_NET_PREDICTION_LIMIT ||
		settings->serial_port < MH_CFG_SERIAL_PORT_MIN ||
		settings->serial_port > MH_CFG_SERIAL_PORT_MAX ||
		settings->baud_rate > 5 || settings->parity > 3 ||
		settings->stop_bits > 2 || settings->flow_control > 4 ||
		settings->menu_confirmation > 2 ||
		settings->menu_cd_track > MH_U32_MAX ||
		settings->laps < MH_RACE_MIN_LAPS ||
		settings->laps > MH_RACE_MAX_LAPS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid settings value");

	return validate_av(&settings->av, error_out);
}

static void
append_av(struct config_writer *writer, const struct mh_av_settings *av)
{
	static const char *const detail[] = {
		"Low", "Medium", "High", "Max", "Custom"};

	static const char *const info[] = {"None", "Selective", "All"};
	static const char *const names[] = {"None", "Flat", "Transparent"};
	static const char *const shading[] = {
		"Flat", "Gouraud", "Reflection", "Glenz"};

	static const char *const correction[] = {"Selective", "All"};
	static const char *const quality[] = {"Normal", "Good"};
	static const char *const toggle[] = {"Off", "On"};
	static const char *const screenshot_format[] = {"PNG", "TGA"};
	static const char *const race_audio[] = {
		"Software", "DS3D", "EAX", "A3D12", "A3D20"};

	unsigned int i;

	append_line(writer, "Fullscreen", toggle[av->fullscreen]);
	append_line(writer, "GrabMouse", toggle[av->grab_mouse]);
	append_line(writer, "ShowFPS", toggle[av->show_fps]);
	append_line(writer, "VSync", toggle[av->vsync]);
	append_integer_line(writer, "MaxFPS", av->max_fps);
	append_line(writer, "ScreenshotFormat",
		screenshot_format[av->screenshot_format]);
	append_text(writer, "ScreenDimension\t");
	append_integer(writer, av->width);
	append_text(writer, " ");
	append_integer(writer, av->height);
	append_text(writer, "\n");
	append_text(writer, "Brightness\t");

	for (i = 0; i < MH_AV_COLOR_CHANNELS; i++) {
		if (i != 0)
			append_text(writer, " ");

		append_decimal(writer,
			(float)((long double)av->brightness[i] * 100.0));
	}

	append_text(writer, "\n");
	append_line(writer, "InfoDetail", info[av->info_detail]);
	append_line(writer, "CheckpointInfo", info[av->checkpoint_info]);
	append_decimal_line(writer, "CheckpointDelay",
		(float)((long double)av->checkpoint_delay * (double).001));
	append_line(writer, "RoadMap", info[av->road_map]);
	append_line(writer, "DetailMode", detail[av->detail_mode]);
	append_line(writer, "LensFlares", toggle[av->lens_flares]);
	append_line(writer, "Sparks", toggle[av->sparks]);
	append_line(writer, "Smoke", toggle[av->smoke]);
	append_line(writer, "Halos", toggle[av->halos]);
	append_line(writer, "Shadow", toggle[av->shadow]);
	append_line(writer, "SkidMarks", toggle[av->skid_marks]);
	append_line(writer, "NamePlates", names[av->name_plates]);
	append_line(writer, "SkyImage", toggle[av->sky_image]);
	append_line(writer, "TrackDetail", detail[1 + av->track_detail]);
	append_line(writer, "CarDetail", detail[av->car_detail]);
	append_line(writer, "CarShading", shading[av->car_shading]);
	append_line(writer, "TextureFiltering",
		av->texture_filter == MH_AV_FILTER_ORIGINAL ? "Original"
			: av->texture_filter == MH_AV_FILTER_NEAREST ? "Nearest"
			: av->texture_filter == MH_AV_FILTER_BILINEAR
			? "Bilinear"
			: "Trilinear");
	append_integer_line(writer, "FrameQueue", av->frame_queue + 1);
	append_line(writer, "TexturePrecision",
		av->texture_precision == MH_AV_TEXTURE_16BIT ? "16" : "32");
	append_line(writer, "PerspectiveOn", correction[av->perspective_on]);
	append_line(writer, "Perspective",
		quality[av->perspective == MH_AV_PERSPECTIVE_GOOD]);
	append_decimal_line(writer, "ViewDistance", av->view_distance);
	append_line(writer, "MotionBlur", toggle[av->motion_blur]);
	append_line(writer, "TrippleBuffer", toggle[av->triple_buffer]);
	append_decimal_line(writer, "SFXVolume",
		(float)((long double)av->effects_volume * 100.0 *
			(double)(1.0 / 255.0)));
	append_decimal_line(writer, "MusicVolume",
		(float)((long double)av->music_volume * 100.0 *
			(double)(1.0 / 255.0)));
	append_line(writer, "CDLoop", toggle[av->cd_loop]);
	append_line(writer, "RaceAudio", race_audio[av->race_audio]);
}

enum mh_error_code
mh_settings_encode(const struct mh_settings *settings,
	const struct mh_settings_catalog *catalog, char *text, size_t capacity,
	size_t *size, struct mh_error *error_out)
{
	static const char *const toggle[] = {"Off", "On", "NoExit"};
	static const char *const gearbox[] = {"Manual", "Auto"};
	static const char *const modes[] = {
		"Single", "League", "TimeAttack", "GhostMode"};

	static const char *const difficulty[] = {"Easy", "Medium", "Hard"};
	static const char *const protocol[] = {
		"IPX", "TCP", "DialUp", "Serial"};

	static const char *const parity[] = {"None", "Odd", "Even", "Mark"};
	static const char *const stops[] = {"1", "2", "1-5"};
	static const char *const flow[] = {
		"None", "DTR", "RTSDTR", "RTS", "XOnXOff"};

	static const unsigned long baud[] = {
		38400UL, 56000UL, 57600UL, 115200UL, 128000UL, 256000UL};

	struct config_writer writer;
	enum mh_error_code status;
	unsigned int i;
	char key[16];

	if (size == NULL || (text == NULL && capacity != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid settings output buffer");

	status = validate_settings(settings, catalog, &writer.error);

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &writer.error);

	writer.size = 0;
	writer.text[0] = 0;
	append_text(&writer, "// Motorhead native configuration\n");
	append_line(&writer, "PlayerName", settings->player_name);
	append_color(&writer, "PlayerColour", settings->player_color);
	append_line(&writer, "TeamName", settings->team_name);
	append_line(&writer, "ControlName", settings->control_name);

	for (i = 0; i < MH_SETTINGS_SHORTCUTS; i++) {
		sprintf(key, "ShortKey%u", i);
		append_line(&writer, key, settings->short_keys[i]);
	}

	append_line(&writer, "CustomCarColours",
		toggle[settings->custom_car_colors]);

	for (i = 0; i < MH_SETTINGS_CAR_COLORS; i++) {
		sprintf(key, "CarColour%u", i + 1);
		append_color(&writer, key, settings->car_colors[i]);
	}

	append_line(&writer, "System",
		settings->av.system == MH_UNITS_IMPERIAL ? "Imperial"
							: "Metric");
	append_line(&writer, "CarName", catalog->cars.names[settings->car]);
	append_line(&writer, "CarTransmission", gearbox[settings->automatic]);
	append_line(&writer, "CarView",
		settings->av.car_view == MH_CAR_VIEW_INSIDE ? "Inside"
							: "Outside");
	append_line(&writer, "CarHorn", catalog->horns.names[settings->horn]);
	append_line(&writer, "GameType", modes[settings->game_type]);
	append_line(&writer, "Difficulty", difficulty[settings->difficulty]);
	append_line(
		&writer, "TrackName", catalog->tracks.names[settings->track]);
	append_integer_line(&writer, "TrackLaps", settings->laps);
	append_line(&writer, "CatchUp", toggle[settings->catch_up]);
	append_line(&writer, "LeagueName", settings->league_name);
	append_line(&writer, "RecordRace", toggle[settings->record_race]);
	append_line(&writer, "DemoFileName", settings->demo_filename);
	append_line(&writer, "Renderer",
		settings->renderer == MH_SETTINGS_NO_RENDERER
			? ""
			: catalog->renderers.names[settings->renderer]);
	append_av(&writer, &settings->av);
	append_line(&writer, "SessionName", settings->session_name);
	append_line(&writer, "SessionPassword", settings->session_password);
	append_line(&writer, "Protocol", protocol[settings->protocol]);
	append_integer_line(&writer, "SerialPort", settings->serial_port);
	append_integer_line(&writer, "BaudRate", baud[settings->baud_rate]);
	append_line(&writer, "Parity", parity[settings->parity]);
	append_line(&writer, "StopBits", stops[settings->stop_bits]);
	append_line(&writer, "FlowControl", flow[settings->flow_control]);
	append_integer_line(
		&writer, "PredictionMin", settings->prediction.minimum);
	append_integer_line(
		&writer, "PredictionMax", settings->prediction.maximum);
	append_integer_line(&writer, "RateTCP", settings->tcp_rate);
	append_line(&writer, "HiscorePath", settings->hiscore_path);
	append_line(&writer, "MenuConfirmation",
		toggle[settings->menu_confirmation]);

	for (i = 0; i < settings->course_music_count; i++) {
		append_text(&writer, "CDTrack_");
		append_integer(&writer, settings->course_music[i].division);
		append_text(&writer, "\t");
		append_integer(&writer, settings->course_music[i].track);
		append_text(&writer, "\n");
	}

	append_integer_line(&writer, "MenuCDTrack", settings->menu_cd_track);

	if (writer.error.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &writer.error);

	if (text != NULL) {
		if (capacity <= writer.size)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"settings output buffer too small");

		memcpy(text, writer.text, writer.size + 1);
	}

	*size = writer.size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
