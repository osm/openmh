#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/settings/control_layout.h"
#include "engine/settings/menu_sound.h"
#include "engine/ui/menu.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Config assets line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_controls(void)
{
	static const unsigned char text[] =
		"// Swedish: \305 \326 \304\r\nLayoutName Mouse\nTurnLeft MouseLeft\n"
		"TurnRight JoyRight\nHorn Space\nHorn Unknown\n"
		"Accelerate JoyButton1 extra\nMouseSpeedH 1,25\nMouseSpeedV -2\n";
	struct mh_control_layout layout, saved;
	const char *names[256];
	unsigned char long_name[43];
	unsigned int i;

	CHECK(mh_control_layout_read(text, sizeof(text) - 1, NULL, &layout,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(layout.name, "Mouse") == 0 &&
		layout.mouse_speed[0] == 1.25 && layout.mouse_speed[1] == -2);
	CHECK(layout.mouse_speed[2] == 1 && layout.force_feedback == 0);
	CHECK(layout.bindings[MH_CONTROL_LEFT] == 249 &&
		layout.bindings[MH_CONTROL_RIGHT] == 244);
	CHECK(layout.bindings[MH_CONTROL_HORN] == 57 &&
		layout.bindings[MH_CONTROL_ACCELERATE] == 200);
	CHECK(layout.bindings[MH_CONTROL_SHIFT_UP] == 30 &&
		layout.bindings[MH_CONTROL_SHIFT_DOWN] == 31);
	saved = layout;
	CHECK(mh_control_layout_read((const unsigned char *)"LayoutName \1",
		strlen("LayoutName \1"), NULL, &layout,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &layout, sizeof(layout)) == 0);
	CHECK(mh_control_layout_read((const unsigned char *)"MouseSpeedV 1e300",
		strlen("MouseSpeedV 1e300"), NULL, &layout,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &layout, sizeof(layout)) == 0);
	memcpy(long_name, "LayoutName ", 11);
	memset(long_name + 11, 'x', 32);
	CHECK(mh_control_layout_read(long_name, 42, NULL, &layout, NULL) ==
		MH_ERROR_NONE);
	CHECK(strlen(layout.name) == 31);
	saved = layout;
	CHECK(mh_control_layout_read(long_name, 43, NULL, &layout, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &layout, sizeof(layout)) == 0);

	for (i = 0; i < 256; i++)
		names[i] = NULL;

	names[12] = "LEFTARROW";
	names[56] = "LeftArrow";
	CHECK(mh_control_layout_read((const unsigned char *)"", 0, names,
		&layout, NULL) == MH_ERROR_NONE);
	CHECK(layout.bindings[MH_CONTROL_LEFT] == 12 &&
		layout.bindings[MH_CONTROL_RIGHT] == MH_CONTROL_UNBOUND);
	CHECK(mh_control_key_name(256) == NULL);

	return 0;
}

static int
check_sounds(void)
{
	static const unsigned char text[] =
		"SampleEvent 999\nVolumeChange 1e300\nSampleName one.wav extra\n"
		"SampleEvent 0 1 37\nVolumeChange 50\nVolumeChange -50\nPitchChange 25\n"
		"SampleName two.wav\nSampleEvent 1 2\n";
	static const char *const invalid[] = {"SampleName ../escape.wav",
		"SampleName C:drive.wav", "SampleName ..",
		"SampleName one.wav\nSampleEvent 38",
		"SampleName one.wav\nSampleEvent -1",
		"SampleName one.wav\nVolumeChange 1e300",
		"SampleName one.wav\nPitchChange 1e-1025"};

	struct mh_menu_sound_config config, saved;
	char many[1024];
	unsigned char long_name[75];
	unsigned int i;

	CHECK(mh_menu_sound_read(text, sizeof(text) - 1, &config, NULL) ==
		MH_ERROR_NONE);
	CHECK(config.count == 2 && config.events[0] == 1 &&
		config.events[1] == 2 && config.events[37] == 1);
	CHECK(config.samples[0].volume == .75 &&
		config.samples[0].pitch == 1.25);
	CHECK(config.samples[1].volume == 1 && config.samples[1].pitch == 1);
	CHECK(mh_menu_sound_event(&config, 0) == 0 &&
		mh_menu_sound_event(&config, 1) == 2);
	CHECK(mh_menu_sound_event(&config, 38) == 0 &&
		mh_menu_sound_event(NULL, 1) == 0);
	CHECK(mh_menu_sound_flash(35) && !mh_menu_sound_flash(37) &&
		!mh_menu_sound_flash((unsigned int)MH_U32_MAX));
	saved = config;

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(mh_menu_sound_read((const unsigned char *)invalid[i],
			strlen(invalid[i]), &config,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&saved, &config, sizeof(config)) == 0);
	}

	many[0] = 0;

	for (i = 0; i < MH_MENU_SOUND_SAMPLES; i++)
		strcat(many, "SampleName s.wav\n");

	CHECK(mh_menu_sound_read((const unsigned char *)many, strlen(many),
		&config, NULL) == MH_ERROR_NONE);
	CHECK(config.count == MH_MENU_SOUND_SAMPLES);
	saved = config;
	strcat(many, "SampleName s.wav\n");
	CHECK(mh_menu_sound_read((const unsigned char *)many, strlen(many),
		&config, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &config, sizeof(config)) == 0);
	memcpy(long_name, "SampleName ", 11);
	memset(long_name + 11, 'x', 64);
	CHECK(mh_menu_sound_read(long_name, 74, &config, NULL) ==
		MH_ERROR_NONE);
	CHECK(strlen(config.samples[0].name) == 63);
	saved = config;
	CHECK(mh_menu_sound_read(long_name, 75, &config, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &config, sizeof(config)) == 0);

	return 0;
}

int
main(void)
{

	CHECK(check_controls() == 0 && check_sounds() == 0);
	puts("Config assets: original defaults, legacy comments, bindings, sound order and atomic bounds passed");

	return 0;
}
