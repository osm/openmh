#include <float.h>
#include <stdio.h>
#include <string.h>

#include "../support/settings_catalog.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/settings/av.h"
#include "engine/settings/course_music.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "settings:%d: %s\n", __LINE__, #x);    \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_full_settings(void)
{
	struct mh_settings settings, old;
	struct mh_settings_catalog catalog;
	unsigned long seed;
	const char *source;
	char too_long[600];

	CHECK(mh_settings_init(&settings, "Fixture Player", 255, 87, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(settings.automatic && settings.horn == 0 && settings.laps == 5 &&
		!settings.catch_up && !settings.record_race);
	CHECK(strcmp(settings.league_name, "Defualt") == 0 &&
		strcmp(settings.team_name, "") == 0);
	source =
		"PlayerName Four  Five\tSix Seven\nTeamName ABCDEFG\nCarTransmission Manual\nCarName Car Beta\n"
		"CarName absent\nCarHorn Honker2\nTrackName Track Beta\nTrackLaps 25\nTrackLaps 2\n"
		"PlayerColour -1 256 257\nCarColour1 30\nCustomCarColours On\nRenderer 3dfx Glide\n"
		"ShortKey9 Hello\tWorld\nSessionPassword password 123456789\nHiscorePath old scores\n"
		"SerialPort -1\nBaudRate 9600\nParity Mark\nStopBits 1-5\nFlowControl XONXOFF\n"
		"MenuConfirmation NoExit\nMenuCDTrack -1\nCarShading Gouraud\n";
	seed = 17;
	CHECK(mh_settings_parse((const unsigned char *)source, strlen(source),
		&settings_catalog, &seed, &settings,
		NULL) == MH_ERROR_NONE);
	CHECK(seed == 17 && settings.car == 1 && settings.track == 1 &&
		settings.horn == 1 && !settings.automatic &&
		settings.renderer == 2);
	CHECK(strcmp(settings.player_name, "Four Five Six S") == 0 &&
		strcmp(settings.team_name, "ABCD") == 0);
	CHECK(settings.player_color == 0xff0001UL &&
		settings.car_colors[0] == 0x1e0000UL && settings.laps == 2);
	CHECK(strcmp(settings.short_keys[9], "Hello World") == 0 &&
		strcmp(settings.hiscore_path, "old scores\\") == 0);
	CHECK(strcmp(settings.session_password, "password 123456") == 0 &&
		settings.custom_car_colors == 1);
	CHECK(settings.serial_port == 4 && settings.baud_rate == 2 &&
		settings.parity == 3 && settings.stop_bits == 2 &&
		settings.flow_control == 4);
	CHECK(settings.menu_confirmation == 2 &&
		settings.menu_cd_track == MH_U32_MAX &&
		settings.av.car_shading == 1);
	source =
		"TrackLaps 0\nTrackLaps 26\nTrackLaps -1\nTrackLaps 7.5\n"
		"TrackLaps 07\nTrackLaps 3laps\nTrackLaps 18446744073709551616\n";
	CHECK(mh_settings_parse((const unsigned char *)source, strlen(source),
		&settings_catalog, &seed, &settings,
		NULL) == MH_ERROR_NONE);
	CHECK(settings.laps == 2);
	old = settings;
	source = "CarHorn ABCD\nPlayerName Changed\nSFXVolume 1e99999\n";
	CHECK(mh_settings_parse((const unsigned char *)source, strlen(source),
		&settings_catalog, &seed, &settings,
		NULL) != MH_ERROR_NONE);
	CHECK(seed == 17 && memcmp(&settings, &old, sizeof(settings)) == 0);
	source = "CarHorn x\n";
	CHECK(mh_settings_parse((const unsigned char *)source, strlen(source),
		&settings_catalog, &seed, &settings,
		NULL) != MH_ERROR_NONE);
	CHECK(seed == 17 && memcmp(&settings, &old, sizeof(settings)) == 0);
	memset(too_long, 'a', sizeof(too_long));
	memcpy(too_long, "ControlName ", 12);
	too_long[sizeof(too_long) - 1] = 0;
	CHECK(mh_settings_parse((const unsigned char *)too_long,
		strlen(too_long), &settings_catalog, &seed, &settings,
		NULL) != MH_ERROR_NONE);
	memcpy(too_long, "HiscorePath ", 12);
	CHECK(mh_settings_parse((const unsigned char *)too_long,
		strlen(too_long), &settings_catalog, &seed, &settings,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&settings, &old, sizeof(settings)) == 0);
	catalog = settings_catalog;
	catalog.horns.count = 0;
	CHECK(mh_settings_parse((const unsigned char *)"", 0, &catalog, &seed,
		&settings, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&settings, &old, sizeof(settings)) == 0);
	catalog = settings_catalog;
	catalog.renderers.count = 0;
	settings.renderer = MH_SETTINGS_NO_RENDERER;
	CHECK(mh_settings_parse((const unsigned char *)"Renderer absent\n", 16,
		&catalog, &seed, &settings, NULL) == MH_ERROR_NONE);
	CHECK(settings.renderer == MH_SETTINGS_NO_RENDERER);

	return 0;
}

static int
check_race_audio(void)
{
	static const char *const lines[] = {"RaceAudio\tSoftware\n",
		"RaceAudio\tDS3D\n", "RaceAudio\tEAX\n", "RaceAudio\tA3D12\n",
		"RaceAudio\tA3D20\n"};

	static const char *const legacy[] = {
		"SpatialAudio Off\n", "SpatialAudio On\n"};

	struct mh_settings settings, loaded;
	char text[MH_SETTINGS_TEXT_CAPACITY];
	const char *source;
	size_t size;
	unsigned long seed;
	unsigned int i;

	CHECK(mh_settings_init(&settings, "Audio", 255, 255, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(settings.av.race_audio == MH_RACE_AUDIO_SOFTWARE);

	for (i = MH_RACE_AUDIO_SOFTWARE; i <= MH_RACE_AUDIO_A3D20; i++) {
		settings.av.race_audio = i;
		CHECK(mh_settings_encode(&settings, &settings_catalog, text,
			sizeof(text), &size, NULL) == MH_ERROR_NONE);
		CHECK(strstr(text, lines[i]) != NULL &&
			strstr(text, "SpatialAudio") == NULL);
		CHECK(mh_settings_init(&loaded, "Reload", 0, 0, 0, NULL) ==
			MH_ERROR_NONE);
		seed = 17;
		CHECK(mh_settings_parse((const unsigned char *)text, size,
			&settings_catalog, &seed, &loaded,
			NULL) == MH_ERROR_NONE);
		CHECK(loaded.av.race_audio == i);
	}

	for (i = 0; i < 2; i++) {
		CHECK(mh_av_settings_parse((const unsigned char *)legacy[i],
			strlen(legacy[i]), &loaded.av,
			NULL) == MH_ERROR_NONE);
		CHECK(loaded.av.race_audio == i);
	}

	source = "RaceAudio eax\nRaceAudio unknown\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &loaded.av, NULL) == MH_ERROR_NONE);
	CHECK(loaded.av.race_audio == MH_RACE_AUDIO_EAX);
	settings.av.race_audio = MH_RACE_AUDIO_A3D20 + 1;
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) == MH_ERROR_INVALID);

	return 0;
}

static int
check_encoding(void)
{
	struct mh_settings settings, loaded, old;
	struct mh_settings_catalog catalog;
	char text[MH_SETTINGS_TEXT_CAPACITY],
		previous[MH_SETTINGS_TEXT_CAPACITY];
	size_t size, required;
	unsigned long seed;
	unsigned int i;

	CHECK(mh_settings_init(&settings, "Ace", 255, 87, 0, NULL) ==
		MH_ERROR_NONE);
	settings.av.system = MH_UNITS_IMPERIAL;
	settings.av.car_view = MH_CAR_VIEW_OUTSIDE;
	settings.av.texture_precision = MH_AV_TEXTURE_16BIT;
	settings.av.texture_filter = MH_AV_FILTER_TRILINEAR;
	settings.av.frame_queue = MH_AV_FRAME_QUEUE_THREE;
	settings.av.triple_buffer = 1;
	settings.av.fullscreen = 1;
	CHECK(settings.av.grab_mouse == 1);
	settings.av.grab_mouse = 0;
	settings.av.show_fps = 1;
	settings.av.vsync = 1;
	settings.av.max_fps = 1001;
	settings.av.screenshot_format = MH_SCREENSHOT_TGA;
	settings.av.race_audio = MH_RACE_AUDIO_DS3D;
	settings.tcp_rate = 37;
	settings.prediction.minimum = 8;
	settings.prediction.maximum = 0;
	settings.av.width = MH_U32_MAX;
	settings.av.height = MH_U32_SIGN_BIT;
	settings.menu_cd_track = MH_U32_MAX;
	settings.av.brightness[0] = -.00001f;
	settings.av.brightness[1] = .01125f;
	settings.av.brightness[2] = .09999f;
	settings.av.checkpoint_delay = 1125;
	settings.av.view_distance = 99.125f;
	strcpy(settings.short_keys[9], "Hello racers");
	strcpy(settings.team_name, "TEAM");
	old = settings;
	size = 0;
	memset(text, 0x5a, sizeof(text));
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) == MH_ERROR_NONE);
	CHECK(size == strlen(text) &&
		memcmp(&old, &settings, sizeof(settings)) == 0);
	CHECK(strstr(text, "System\tImperial\n") != NULL);
	CHECK(strstr(text, "CarView\tOutside\n") != NULL);
	CHECK(strstr(text, "TexturePrecision\t16\n") != NULL);
	CHECK(strstr(text, "TextureFiltering\tTrilinear\n") != NULL);
	CHECK(strstr(text, "ScreenDimension\t-1 -2147483648\n") != NULL);
	CHECK(strstr(text, "MenuCDTrack\t-1\n") != NULL);
	CHECK(strstr(text, "Brightness\t0.00 1.13 10.00\n") != NULL);
	CHECK(strstr(text, "CheckpointDelay\t1.13\n") != NULL);
	CHECK(strstr(text, "ViewDistance\t99.13\n") != NULL);
	CHECK(strstr(text, "SFXVolume\t100.00\n") != NULL);
	CHECK(mh_settings_encode(&settings, &settings_catalog, NULL, 0,
		&required, NULL) == MH_ERROR_NONE &&
		required == size);
	CHECK(mh_settings_init(&loaded, "Other", 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	seed = 17;
	CHECK(mh_settings_parse((const unsigned char *)text, size,
		&settings_catalog, &seed, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(loaded.av.system == MH_UNITS_IMPERIAL);
	CHECK(loaded.av.car_view == MH_CAR_VIEW_OUTSIDE);
	CHECK(loaded.av.texture_precision == MH_AV_TEXTURE_16BIT);
	CHECK(loaded.av.texture_filter == MH_AV_FILTER_TRILINEAR);
	CHECK(loaded.av.frame_queue == MH_AV_FRAME_QUEUE_THREE);
	CHECK(loaded.av.triple_buffer == 1);
	CHECK(loaded.av.fullscreen == 1);
	CHECK(loaded.av.grab_mouse == 0);
	CHECK(loaded.av.show_fps == 1);
	CHECK(loaded.av.vsync == 1 && loaded.av.max_fps == 1001);
	CHECK(loaded.av.screenshot_format == MH_SCREENSHOT_TGA);
	CHECK(strstr(text, "ScreenshotFormat\tTGA\n") != NULL);
	CHECK(strstr(text, "VSync\tOn\n") != NULL);
	CHECK(strstr(text, "MaxFPS\t1001\n") != NULL);
	CHECK(loaded.av.race_audio == MH_RACE_AUDIO_DS3D);
	CHECK(strstr(text, "RaceAudio\tDS3D\n") != NULL);
	CHECK(strstr(text, "TrippleBuffer\tOn\n") != NULL);
	CHECK(strstr(text, "FrameQueue\t3\n") != NULL);
	CHECK(loaded.tcp_rate == 37);
	CHECK(loaded.prediction.minimum == 8 && loaded.prediction.maximum == 0);
	CHECK(loaded.av.effects_volume == 254 && loaded.av.music_volume == 87 &&
		loaded.av.checkpoint_delay == 1130);
	CHECK(loaded.av.view_distance == 99.13f &&
		loaded.av.width == MH_U32_MAX &&
		loaded.av.height == MH_U32_SIGN_BIT);
	CHECK(strcmp(loaded.player_name, "Ace") == 0 &&
		strcmp(loaded.short_keys[9], "Hello racers") == 0 &&
		seed == 17);
	memcpy(previous, text, sizeof(text));
	CHECK(mh_settings_encode(&settings, &settings_catalog, text, size,
		&size, NULL) != MH_ERROR_NONE);
	CHECK(size == required && memcmp(previous, text, sizeof(text)) == 0);
	CHECK(mh_settings_encode(&settings, &settings_catalog, text, size + 1,
		&size, NULL) == MH_ERROR_NONE);
	settings.av.car_view = 2;
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) != MH_ERROR_NONE);
	settings = old;
	settings.av.texture_precision = MH_AV_TEXTURE_32BIT + 1;
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) != MH_ERROR_NONE);
	settings = old;
	settings.flow_control = 5;
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) != MH_ERROR_NONE);
	settings = old;
	settings.av.brightness[0] = -FLT_MAX;
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) != MH_ERROR_NONE);
	settings = old;
	settings.car = 4096;
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) != MH_ERROR_NONE);
	settings = old;
	memset(settings.player_name, 'x', sizeof(settings.player_name));
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) != MH_ERROR_NONE);
	settings = old;
	strcpy(settings.player_name, "Name\nHalos Off");
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) != MH_ERROR_NONE);
	CHECK(size == required && memcmp(previous, text, sizeof(text)) == 0);
	settings = old;
	strcpy(settings.player_name, "Name;comment");
	strcpy(settings.team_name, "A//B");
	strcpy(settings.short_keys[9], "Hello // racers");
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) == MH_ERROR_NONE);
	CHECK(strstr(text, "PlayerName\tName;comment\n") != NULL &&
		strstr(text, "TeamName\tA//B\n") != NULL &&
		strstr(text, "ShortKey9\tHello // racers\n") != NULL);
	CHECK(mh_settings_init(&loaded, "Other", 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	seed = 17;
	CHECK(mh_settings_parse((const unsigned char *)text, size,
		&settings_catalog, &seed, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(loaded.player_name, "Name") == 0 &&
		strcmp(loaded.team_name, "A") == 0 &&
		strcmp(loaded.short_keys[9], "Hello") == 0);
	settings = old;
	catalog = settings_catalog;
	catalog.renderers.count = 0;
	settings.renderer = MH_SETTINGS_NO_RENDERER;
	CHECK(mh_settings_encode(&settings, &catalog, text, sizeof(text), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(strstr(text, "Renderer\t\n") != NULL);

	/* All text fields at capacity still fit the advertised output bound. */
	for (i = 0; i < 10; i++) {
		memset(settings.short_keys[i], 'x', 127);
		settings.short_keys[i][127] = 0;
	}

	memset(settings.control_name, 'x', 127);
	settings.control_name[127] = 0;
	memset(settings.hiscore_path, 'x', 510);
	settings.hiscore_path[510] = '\\';
	settings.hiscore_path[511] = 0;
	CHECK(mh_settings_encode(&settings, &catalog, text, sizeof(text), &size,
		NULL) == MH_ERROR_NONE);

	return 0;
}

static int
check_legacy_text(void)
{
	struct mh_settings settings, loaded, before;
	char text[MH_SETTINGS_TEXT_CAPACITY];
	size_t size;
	unsigned int byte;
	unsigned long seed;

	for (byte = 127; byte < 256; byte++) {
		CHECK(mh_settings_init(&settings, "A\xe5\xe4\xf6", 0, 0, 0,
			NULL) == MH_ERROR_NONE);
		settings.player_name[1] = (char)byte;
		strcpy(settings.team_name, settings.player_name);
		strcpy(settings.league_name, settings.player_name);
		strcpy(settings.demo_filename, settings.player_name);
		strcpy(settings.session_name, settings.player_name);
		strcpy(settings.session_password, settings.player_name);
		strcpy(settings.short_keys[9], settings.player_name);
		CHECK(mh_settings_encode(&settings, &settings_catalog, text,
			sizeof(text), &size, NULL) == MH_ERROR_NONE);
		CHECK(mh_settings_init(&loaded, "Other", 0, 0, 0, NULL) ==
			MH_ERROR_NONE);
		seed = 17;
		CHECK(mh_settings_parse((const unsigned char *)text, size,
			&settings_catalog, &seed, &loaded,
			NULL) == MH_ERROR_NONE);
		CHECK(seed == 17 &&
			strcmp(loaded.player_name, settings.player_name) == 0 &&
			strcmp(loaded.team_name, settings.team_name) == 0 &&
			strcmp(loaded.league_name, settings.league_name) == 0 &&
			strcmp(loaded.demo_filename, settings.demo_filename) ==
				0 &&
			strcmp(loaded.session_name, settings.session_name) ==
				0 &&
			strcmp(loaded.session_password,
				settings.session_password) == 0 &&
			strcmp(loaded.short_keys[9], settings.short_keys[9]) ==
				0);
		before = loaded;
		text[size++] = 1;
		CHECK(mh_settings_parse((const unsigned char *)text, size,
			&settings_catalog, &seed, &loaded,
			NULL) != MH_ERROR_NONE);
		CHECK(seed == 17 &&
			memcmp(&before, &loaded, sizeof(loaded)) == 0);
	}

	return 0;
}

static int
check_course_music(void)
{
	struct mh_settings settings, loaded, before;
	struct mh_track tracks[3], reordered[3];
	char text[MH_SETTINGS_TEXT_CAPACITY];
	const char *source;
	size_t size;
	unsigned long seed;
	unsigned int i;

	CHECK(mh_settings_init(&settings, "Music", 255, 255, 0, NULL) ==
		MH_ERROR_NONE);
	memset(tracks, 0, sizeof(tracks));

	for (i = 0; i < 3; i++) {
		tracks[i].has_division = 1;
		tracks[i].division = i == 0 ? 101 : i == 1 ? 111 : 100;
		tracks[i].cd_track = i == 2 ? 11 : 4;
	}

	CHECK(mh_settings_music_bind(&settings, tracks, 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_settings_track_music(NULL, &tracks[0]) == 4);
	source =
		"CDTrack_101 6\ncdtrack_111 8\nCDTrack_100 12\nCDTrack_999 7\nCDTrack_0101 9\n";
	seed = 17;
	CHECK(mh_settings_parse((const unsigned char *)source, strlen(source),
		&settings_catalog, &seed, &settings,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_track_music(&settings, &tracks[0]) == 6 &&
		mh_settings_track_music(&settings, &tracks[1]) == 8 &&
		mh_settings_track_music(&settings, &tracks[2]) == 11);
	CHECK(seed == 17 && settings.menu_cd_track == 3);
	CHECK(mh_settings_encode(&settings, &settings_catalog, text,
		sizeof(text), &size, NULL) == MH_ERROR_NONE);
	CHECK(strstr(text,
		"CDTrack_101\t6\nCDTrack_111\t8\nCDTrack_100\t11\n") !=
		NULL);
	CHECK(mh_settings_init(&loaded, "Reload", 255, 255, 0, NULL) ==
		MH_ERROR_NONE);
	reordered[0] = tracks[2];
	reordered[1] = tracks[0];
	reordered[2] = tracks[1];
	CHECK(mh_settings_music_bind(&loaded, reordered, 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_settings_parse((const unsigned char *)text, size,
		&settings_catalog, &seed, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(loaded.course_music[0].track == 11 &&
		loaded.course_music[1].track == 6 &&
		loaded.course_music[2].track == 8);
	CHECK(mh_settings_music_bind(&loaded, tracks, 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(loaded.course_music[0].track == 6 &&
		loaded.course_music[1].track == 8);
	before = loaded;
	source = "CDTrack_101 9\nCDTrack_111 1e99999\n";
	CHECK(mh_settings_parse((const unsigned char *)source, strlen(source),
		&settings_catalog, &seed, &loaded,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &before, sizeof(loaded)) == 0);

	return 0;
}

int
main(void)
{
	struct mh_av_settings settings, saved;
	unsigned char oversized[1040];
	const char *source;
	unsigned int i;

	CHECK(check_course_music() == 0);
	CHECK(check_full_settings() == 0);
	CHECK(check_encoding() == 0);
	CHECK(check_legacy_text() == 0);
	CHECK(check_race_audio() == 0);
	CHECK(mh_av_settings_init(&settings, 255, 87, NULL) == MH_ERROR_NONE);
	CHECK(settings.system == MH_UNITS_METRIC);
	CHECK(settings.vsync == 1 && settings.max_fps == 0);
	CHECK(settings.screenshot_format == MH_SCREENSHOT_PNG);
	saved = settings;
	source = "VSync Off\nMaxFPS 10001\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &settings, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&saved, &settings, sizeof(settings)) == 0);
	source = "MaxFPS 1.5\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &settings, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&saved, &settings, sizeof(settings)) == 0);
	CHECK(settings.car_view == MH_CAR_VIEW_INSIDE);
	CHECK(settings.texture_precision == MH_AV_TEXTURE_32BIT);
	CHECK(settings.texture_filter == MH_AV_FILTER_ORIGINAL);
	CHECK(settings.race_audio == 0);
	CHECK(settings.frame_queue == MH_AV_FRAME_QUEUE_TWO);
	CHECK(settings.triple_buffer == 0);
	CHECK(settings.view_distance == 80 &&
		settings.detail_mode == MH_DETAIL_HIGH &&
		settings.width == 320 && settings.height == 200);
	source =
		"CarView oUtSiDe\nCarView unknown\nSystem Imperial\nSystem unknown\nDetailMode Low\r\nViewDistance 145.25\nSFXVolume 100\nMusicVolume 100.1\n"
		"Brightness 50,5 250 -20\nHalos No\nHalos YES// comment\n"
		"CheckpointDelay -1\nPerspective Good; comment\nScreenDimension 800\n"
		"RoadMap None\nRoadMap typo\nUnknown Field\nSmoke\n"
		"TexturePrecision 16\nTexturePrecision unknown\nFrameQueue 1\nFrameQueue 0\nFrameQueue 4\n"
		"TextureFiltering Nearest\nTextureFiltering Bilinear\nTextureFiltering Trilinear\nTextureFiltering unknown\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &settings, NULL) == MH_ERROR_NONE);
	CHECK(settings.car_view == MH_CAR_VIEW_OUTSIDE);
	CHECK(settings.detail_mode == MH_DETAIL_LOW &&
		settings.view_distance == 145.25f && settings.shadow == 1);
	CHECK(settings.effects_volume == 254 && settings.music_volume == 255 &&
		settings.checkpoint_delay == 10000);
	CHECK(settings.width == 800 && settings.height == 200 &&
		settings.road_map == 0 && settings.perspective == 16);
	CHECK(settings.halos && settings.smoke &&
		settings.brightness[0] == .505f &&
		settings.brightness[1] == 2 && settings.brightness[2] == -.2f);
	CHECK(mh_av_settings_preset(&settings, NULL) == MH_ERROR_NONE &&
		settings.view_distance == 50 && !settings.shadow &&
		!settings.smoke);
	CHECK(settings.texture_precision == MH_AV_TEXTURE_16BIT);
	source = "GlenzMapPrecision 32\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &settings, NULL) == MH_ERROR_NONE);
	CHECK(settings.texture_precision == MH_AV_TEXTURE_32BIT);
	source =
		"TexturePrecision 16\nTexturePrecision unknown\nFrameQueue 1\nFrameQueue 0\nFrameQueue 4\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &settings, NULL) == MH_ERROR_NONE);
	CHECK(settings.texture_precision == MH_AV_TEXTURE_16BIT);

	CHECK(settings.texture_filter == MH_AV_FILTER_TRILINEAR);
	CHECK(settings.frame_queue == MH_AV_FRAME_QUEUE_ONE);
	source = "tripplebuffer On\nTrippleBuffer unknown\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &settings, NULL) == MH_ERROR_NONE);
	CHECK(settings.triple_buffer == 1);
	CHECK(mh_av_settings_preset(&settings, NULL) == MH_ERROR_NONE &&
		settings.triple_buffer == 1);
	CHECK(settings.system == MH_UNITS_IMPERIAL);
	CHECK(settings.effects_volume == 254 && settings.brightness[1] == 2 &&
		settings.width == 800);
	settings.detail_mode = MH_DETAIL_CUSTOM;
	saved = settings;
	CHECK(mh_av_settings_preset(&settings, NULL) == MH_ERROR_NONE &&
		memcmp(&settings, &saved, sizeof(settings)) == 0);

	/*
	 * Malformed/resource-exhausting input is atomic, including late
	 * failures.
	 */
	memset(oversized, 'a', sizeof(oversized));
	CHECK(mh_av_settings_parse(oversized, sizeof(oversized), &settings,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&settings, &saved, sizeof(settings)) == 0);
	source = "Halos Off\nSFXVolume 1e65536\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &settings, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&settings, &saved, sizeof(settings)) == 0);
	source = "Halos Off\nScreenDimension 1e100\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &settings, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&settings, &saved, sizeof(settings)) == 0);
	source = "Halos Off\nBrightness -1e100\n";
	CHECK(mh_av_settings_parse((const unsigned char *)source,
		strlen(source), &settings, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&settings, &saved, sizeof(settings)) == 0);

	for (i = 0; i < 129; i++) {
		oversized[i * 2] = 'x';
		oversized[i * 2 + 1] = ' ';
	}

	CHECK(mh_av_settings_parse(oversized, 258, &settings, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < 256; i++) {
		if (i >= 32 || i == '\t' || i == '\r' || i == '\n')
			continue;

		oversized[0] = (unsigned char)i;
		CHECK(mh_av_settings_parse(oversized, 1, &settings, NULL) !=
			MH_ERROR_NONE);
	}

	CHECK(memcmp(&settings, &saved, sizeof(settings)) == 0);
	CHECK(mh_av_settings_parse(NULL, 0, &settings, NULL) != MH_ERROR_NONE);
	CHECK(mh_av_settings_parse(oversized, 0, NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_av_settings_parse(oversized, 1048577UL, &settings, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_av_settings_init(&settings, 256, 0, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&settings, &saved, sizeof(settings)) == 0);
	settings.detail_mode = 5;
	saved = settings;
	CHECK(mh_av_settings_preset(&settings, NULL) != MH_ERROR_NONE &&
		memcmp(&settings, &saved, sizeof(settings)) == 0);
	puts("Settings parsing, preset separation, original numeric quirks and bounded atomic failures passed");

	return 0;
}
