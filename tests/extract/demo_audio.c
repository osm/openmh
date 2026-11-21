#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "engine/assets/prepare_demo_audio.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/audio/pause_sound_assets.h"
#include "engine/audio/race_sound_assets.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "demo audio line %d: %s\n", __LINE__,  \
				#expression);                                  \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_playback(const struct mh_sound_bank *bank,
	const struct mh_pause_sound_bank *pause)
{
	static struct mh_field field;
	struct mh_race_sound sound;
	struct mh_mixer menu;
	unsigned int i;
	float mixed[256];

	memset(&field, 0, sizeof(field));
	field.count = 1;
	field.active_mask = field.listener_mask = 1;
	field.cars[0].vehicle.engine.rpm = 5000;
	field.cars[0].vehicle.motion.rotation[0] = 1;
	field.cars[0].vehicle.motion.rotation[4] = 1;
	field.cars[0].vehicle.motion.rotation[8] = 1;
	field.cars[0].camera.rotation[0] = 1;
	field.cars[0].camera.rotation[4] = 1;
	field.cars[0].camera.rotation[8] = 1;
	field.cars[0].target[2] = 1;
	CHECK(mh_race_sound_init(22050, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_vehicles(&sound, bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[MH_SOUND_GAS_ON].active);
	CHECK(mh_mixer_render(&sound.mixer, mixed, 128, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_message(&sound, bank, 1, 0, NULL) == MH_ERROR_NONE);

	if (bank->absent_samples & (1U << MH_SOUND_MESSAGE))
		CHECK(!sound.mixer.voices[MH_RACE_VOICE_MESSAGE].active);

	field.cars[0].race.finished = 1;
	CHECK(mh_race_sound_cues(&sound, bank, &field, 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.finish_latch == 1);

	if (bank->absent_samples & (1U << MH_SOUND_FINISH)) {
		CHECK(!sound.mixer.voices[MH_RACE_VOICE_FINISH].active);
		CHECK(!sound.mixer.voices[MH_RACE_VOICE_FINISH_COPY].active);
	}

	field.race_notice = MH_RACE_NOTICE_OUT;
	field.race_notice_revision = 1;
	CHECK(mh_race_sound_cues(&sound, bank, &field, 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.notice_revision == 1);

	if (bank->absent_samples & (1U << MH_SOUND_ELIMINATED))
		CHECK(!sound.mixer.voices[MH_RACE_VOICE_ELIMINATED].active);

	CHECK(mh_pause_sound_init(22050, 1, &menu, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_PAUSE_SOUND_SAMPLES; i++) {
		CHECK(mh_pause_sound_play(&menu, pause, i, NULL) ==
			MH_ERROR_NONE);

		if (pause->absent_samples & (1U << i))
			CHECK(!menu.voices[i].active);
	}

	return 0;
}

static int
check_assets(const struct mh_resource_source *source, const char *root)
{
	struct mh_race_sound_assets race;
	struct mh_pause_sound_assets pause;
	struct mh_error error;
	enum mh_error_code status;

	status = mh_race_sound_assets_load_from(mh_horn_fallback_source(),
		mh_audio_fallback_decoder(), source, root, NULL, &race, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	CHECK(status == MH_ERROR_NONE);
	CHECK(mh_pause_sound_assets_load_from(mh_audio_fallback_decoder(),
		source, root, &pause, NULL) == MH_ERROR_NONE);
	CHECK(check_playback(&race.bank, &pause.bank) == 0);
	printf("race absent=%04x pause absent=%x\n", race.bank.absent_samples,
		pause.bank.absent_samples);
	mh_race_sound_assets_free(&race);
	mh_pause_sound_assets_free(&pause);

	return 0;
}

static int
check_absence(void)
{
	static const char *paths[] = {"gason.wav", "gasrele.wav", "gasotl.wav",
		"gasotr.wav", "gasrtl.wav", "gasrtr.wav", "inneen.wav",
		"enemyeng.wav", "gear3.wav", "starten/!32.wav",
		"starten/go!.wav", "starten/checkpoint.wav"};

	static const unsigned char wave[46] = {'R', 'I', 'F', 'F', 38, 0, 0, 0,
		'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
		0x40, 0x1f, 0, 0, 0x40, 0x1f, 0, 0, 1, 0, 8, 0, 'd', 'a', 't',
		'a', 2, 0, 0, 0, 128, 160};

	static const unsigned char horn[] = "Name None\nSampleName <None>\n";
	struct mh_resource_entry entries[14];
	char names[12][80];
	const struct mh_resource_source *source;
	struct mh_race_sound_assets race, before;
	struct mh_pause_sound_assets pause, saved_pause;
	unsigned int i;

	for (i = 0; i < 12; i++) {
		sprintf(names[i], "root/Sounds/%s", paths[i]);
		entries[i].path = names[i];
		entries[i].data = wave;
		entries[i].size = sizeof(wave);
	}

	entries[12].path = "root/Sounds/CarHorns/None.chf";
	entries[12].data = horn;
	entries[12].size = sizeof(horn) - 1;
	CHECK(mh_memory_resources_open(entries, 13, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_assets(source, "root") == 0);
	CHECK(mh_race_sound_assets_load_from(mh_horn_fallback_source(),
		mh_audio_fallback_decoder(), source, "root", NULL, &race,
		NULL) == MH_ERROR_NONE);
	CHECK(race.bank.absent_samples ==
		((1U << MH_SOUND_TURBO) | (1U << MH_SOUND_FINISH) |
			(1U << MH_SOUND_ELIMINATED) |
			(1U << MH_SOUND_MESSAGE)));
	mh_race_sound_assets_free(&race);
	mh_memory_resources_close(source);

	entries[13].path = "root/Sounds/finish2.wav";
	entries[13].data = (const unsigned char *)"broken";
	entries[13].size = 6;
	CHECK(mh_memory_resources_open(entries, 14, &source, NULL) ==
		MH_ERROR_NONE);
	memset(&before, 0, sizeof(before));
	race = before;
	CHECK(mh_race_sound_assets_load_from(mh_horn_fallback_source(),
		mh_audio_fallback_decoder(), source, "root", NULL, &race,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&race, &before, sizeof(race)) == 0);
	mh_memory_resources_close(source);
	entries[13].path = "root/Sounds/enter.wav";
	CHECK(mh_memory_resources_open(entries, 14, &source, NULL) ==
		MH_ERROR_NONE);
	memset(&saved_pause, 0, sizeof(saved_pause));
	pause = saved_pause;
	CHECK(mh_pause_sound_assets_load_from(mh_audio_fallback_decoder(),
		source, "root", &pause, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&pause, &saved_pause, sizeof(pause)) == 0);
	mh_memory_resources_close(source);

	/* Dropping a required engine sample still fails. */
	CHECK(mh_memory_resources_open(entries + 1, 12, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_assets_load_from(mh_horn_fallback_source(),
		mh_audio_fallback_decoder(), source, "root", NULL, &race,
		NULL) == MH_ERROR_NOT_FOUND);
	mh_memory_resources_close(source);

	return 0;
}

static int
check_aliases(void)
{
	static const unsigned char bytes[] = "original sample bytes";
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *source;
	const struct mh_user_data *data;
	struct mh_demo_recipe recipe;
	struct mh_file file;
	unsigned int i;
	static const char *const paths[] = {
		"root/Sounds/barrel.wav", "root/Sounds/crash2.wav"};

	memset(&recipe, 0, sizeof(recipe));
	recipe.track_layout = MH_DEMO_TRACK_SOFTWARE_FIRST;
	entries[0].path = "root/Sounds/MajCrash.WAV";
	entries[0].data = bytes;
	entries[0].size = sizeof(bytes);
	CHECK(mh_memory_resources_open(entries, 1, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_audio_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 2; i++) {
		CHECK(mh_user_data_read(data, paths[i], 1024, &file, NULL) ==
			MH_ERROR_NONE);
		CHECK(file.size == sizeof(bytes) &&
			memcmp(file.data, bytes, sizeof(bytes)) == 0);
		free(file.data);
	}

	mh_memory_resources_close(source);
	mh_memory_user_data_close(data);
	entries[1] = entries[0];
	entries[1].path = "root/Sounds/Crash2.WAV";
	CHECK(mh_memory_resources_open(entries, 2, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_audio_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_EXISTS);
	CHECK(mh_user_data_read(data, paths[0], 1024, &file, NULL) ==
		MH_ERROR_NOT_FOUND);
	recipe.track_layout = MH_DEMO_TRACK_GLIDE;
	CHECK(mh_demo_audio_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, paths[0], 1024, &file, NULL) ==
		MH_ERROR_NOT_FOUND);
	mh_memory_resources_close(source);
	mh_memory_user_data_close(data);

	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc == 1)
		return check_absence() || check_aliases();

	if (argc == 2)
		return check_assets(mh_filesystem_resources(), argv[1]);

	fprintf(stderr, "Usage: test-demo-audio [PREPARED-GAME]\n");

	return 1;
}
