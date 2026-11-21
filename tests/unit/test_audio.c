#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/wave.h"
#include "engine/audio/pcm.h"
#include "engine/audio/race_sound.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define WAV_STORAGE_BYTES 64
#define WAV_BYTES 52

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
put_u16le(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
}

static void
put_u32le(unsigned char *bytes, unsigned long value)
{
	put_u16le(bytes, value);
	put_u16le(bytes + 2, value >> 16);
}

static void
wave_fixture(unsigned char data[WAV_STORAGE_BYTES])
{
	memset(data, 0, WAV_STORAGE_BYTES);
	memcpy(data, "RIFF", 4);
	put_u32le(data + 4, 44);
	memcpy(data + 8, "WAVEfmt ", 8);
	put_u32le(data + 16, 16);
	put_u16le(data + 20, 1);
	put_u16le(data + 22, 1);
	put_u32le(data + 24, 8000);
	put_u32le(data + 28, 16000);
	put_u16le(data + 32, 2);
	put_u16le(data + 34, 16);
	memcpy(data + 36, "data", 4);
	put_u32le(data + 40, 8);
	put_u16le(data + 44, 0);
	put_u16le(data + 46, 16384);
	put_u16le(data + 48, 32768);
	put_u16le(data + 50, MH_U16_MAX);
}

static void
wave(void)
{
	unsigned char data[WAV_STORAGE_BYTES], changed[WAV_STORAGE_BYTES];
	struct mh_audio_pcm sample, before;
	unsigned int size;

	wave_fixture(data);
	CHECK(mh_audio_wave_decode(data, WAV_BYTES, &sample, NULL) ==
		MH_ERROR_NONE);
	CHECK(sample.rate == 8000 && sample.frames == 4 &&
		sample.channels == 1 && sample.bits == 16);
	CHECK(mh_audio_pcm_sample(&sample, 0, 0) == 0 &&
		mh_audio_pcm_sample(&sample, 1, 0) == .5);
	CHECK(mh_audio_pcm_sample(&sample, 2, 0) == -1 &&
		mh_audio_pcm_sample(&sample, 3, 0) == -1.0 / 32768);
	before = sample;

	for (size = 0; size < WAV_BYTES; size++) {
		CHECK(mh_audio_wave_decode(data, size, &sample, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&sample, &before, sizeof(sample)) == 0);
	}

	memcpy(changed, data, WAV_STORAGE_BYTES);
	put_u32le(changed + 40, MH_U32_MAX);
	CHECK(mh_audio_wave_decode(changed, WAV_BYTES, &sample, NULL) !=
		MH_ERROR_NONE);
	memcpy(changed, data, WAV_STORAGE_BYTES);
	put_u16le(changed + 20, 3);
	CHECK(mh_audio_wave_decode(changed, WAV_BYTES, &sample, NULL) !=
		MH_ERROR_NONE);
	memcpy(changed, data, WAV_STORAGE_BYTES);
	put_u16le(changed + 32, 3);
	CHECK(mh_audio_wave_decode(changed, WAV_BYTES, &sample, NULL) !=
		MH_ERROR_NONE);
	memcpy(changed, data, WAV_STORAGE_BYTES);
	put_u32le(changed + 24, 192001);
	CHECK(mh_audio_wave_decode(changed, WAV_BYTES, &sample, NULL) !=
		MH_ERROR_NONE);
	memcpy(changed, data, WAV_STORAGE_BYTES);
	put_u32le(changed + 40, 7);
	CHECK(mh_audio_wave_decode(changed, WAV_BYTES, &sample, NULL) !=
		MH_ERROR_NONE);

	/* Extra odd chunk, explicitly padded. */
	memcpy(changed, data, WAV_STORAGE_BYTES);
	put_u32le(changed + 4, 54);
	memcpy(changed + WAV_BYTES, "JUNK", 4);
	put_u32le(changed + 56, 1);
	CHECK(mh_audio_wave_decode(changed, 62, &sample, NULL) ==
		MH_ERROR_NONE);
	put_u32le(changed + 4, 53);
	CHECK(mh_audio_wave_decode(changed, 61, &sample, NULL) !=
		MH_ERROR_NONE);
	wave_fixture(data);
	put_u16le(data + 34, 8);
	put_u16le(data + 32, 1);
	put_u32le(data + 28, 8000);
	data[44] = 0;
	data[45] = 128;
	data[46] = 255;
	CHECK(mh_audio_wave_decode(data, WAV_BYTES, &sample, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_audio_pcm_sample(&sample, 0, 0) == -1 &&
		mh_audio_pcm_sample(&sample, 1, 0) == 0 &&
		mh_audio_pcm_sample(&sample, 2, 0) == 127.0 / 128);
	put_u16le(data + 22, 2);
	put_u16le(data + 32, 2);
	put_u32le(data + 28, 16000);
	CHECK(mh_audio_wave_decode(data, WAV_BYTES, &sample, NULL) ==
			MH_ERROR_NONE &&
		sample.frames == 4);
	CHECK(mh_audio_pcm_sample(&sample, 0, 1) == 0 &&
		mh_audio_pcm_sample(&sample, 1, 0) == 127.0 / 128);
}

static void
mixing(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_audio_pcm sample;
	struct mh_mixer mixer, saved;
	float out[16], whole[16], chunks[16];
	unsigned int i;

	wave_fixture(data);
	CHECK(mh_audio_wave_decode(data, WAV_BYTES, &sample, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_init(8000, 1, &mixer, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(&mixer, 0, &sample, 1, 1, .5, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_render(&mixer, out, 5, 0, NULL) == MH_ERROR_NONE);
	CHECK(out[2] == .5f && out[3] == .25f && out[4] == -1 && out[8] == 0 &&
		!mixer.voices[0].active);
	CHECK(mh_mixer_play(&mixer, 0, &sample, .5, 1, 1, 0, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_render(&mixer, out, 4, 0, NULL) == MH_ERROR_NONE);
	CHECK(out[0] == 0 && out[2] == .25f && out[4] == .5f &&
		out[6] == -.25f);
	saved = mixer;
	CHECK(mh_mixer_render(&mixer, out, 4, 1, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&mixer, &saved, sizeof(mixer)) == 0);

	for (i = 0; i < 8; i++)
		CHECK(out[i] == 0);

	CHECK(mh_mixer_render(&mixer, whole, 8, 0, NULL) == MH_ERROR_NONE);
	mixer = saved;
	CHECK(mh_mixer_render(&mixer, chunks, 3, 0, NULL) == MH_ERROR_NONE &&
		mh_mixer_render(&mixer, chunks + 6, 5, 0, NULL) ==
			MH_ERROR_NONE);
	CHECK(memcmp(whole, chunks, sizeof(whole)) == 0);
	CHECK(mh_mixer_play(&mixer, 0, &sample, 16, 1, 1, 1, 2, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_render(&mixer, out, 3, 0, NULL) == MH_ERROR_NONE &&
		out[0] == 0 && out[2] == .5f && out[4] == .5f);
	saved = mixer;
	CHECK(mh_mixer_play(&mixer, 0, &sample, 1e100, 1, 1, 0, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_mixer_play(&mixer, 0, &sample, 1, 1, 1, 0, 5, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_mixer_adjust(&mixer, MH_MIXER_VOICES, 1, 1, 1, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&mixer, &saved, sizeof(mixer)) == 0);

	for (i = 0; i < 4; i++)
		CHECK(mh_mixer_play(&mixer, i, &sample, 1, 1, 1, 0, 4, NULL) ==
			MH_ERROR_NONE);

	CHECK(mh_mixer_render(&mixer, out, 3, 0, NULL) == MH_ERROR_NONE &&
		out[2] == 1 && out[4] == -1);
	CHECK(mh_mixer_init(8000, 1, &mixer, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(&mixer, 0, &sample, 0, 1, 1, 0, 4, NULL) ==
			MH_ERROR_NONE &&
		mh_mixer_render(&mixer, out, 1, 0, NULL) == MH_ERROR_NONE);
	CHECK(mixer.voices[0].cursor == 22050.0 / 8000 &&
		mixer.voices[0].pitch == 0);
	CHECK(mh_mixer_play(&mixer, 0, &sample, -1, 1, 1, 0, 4, NULL) ==
			MH_ERROR_NONE &&
		mh_mixer_render(&mixer, out, 1, 0, NULL) == MH_ERROR_NONE);
	CHECK(mixer.voices[0].cursor == 22050.0 / 8000 &&
		mixer.voices[0].pitch == -1);
	CHECK(mh_mixer_play(&mixer, 0, &sample, 1e30, 1, 1, 0, 4, NULL) ==
			MH_ERROR_NONE &&
		mh_mixer_render(&mixer, out, 1, 0, NULL) == MH_ERROR_NONE);

	/* 200000/8000 = 25, wrapped over four samples. */
	CHECK(mixer.voices[0].cursor == 1);
}

static void
engine(void)
{
	struct mh_sound_parameters layers[MH_ENGINE_SAMPLES],
		before[MH_ENGINE_SAMPLES];

	CHECK(mh_engine_sound(1000, 1, 0, 1, layers, NULL) == MH_ERROR_NONE);
	CHECK(layers[0].pitch == (double)(float).48746 &&
		layers[0].left == .9 && layers[1].left == 0);
	CHECK(layers[6].left == .25 && layers[7].left == 0);
	CHECK(mh_engine_sound(9000, 0, 1, 0, layers, NULL) == MH_ERROR_NONE);
	CHECK(layers[0].left == 0 && layers[2].left == .9 &&
		layers[2].right == 0 && layers[3].right == .9);
	CHECK(fabs(layers[4].left - .81) < 1e-7 && layers[6].left == 0 &&
		fabs(layers[7].left - .177) < 1e-12);
	CHECK(mh_engine_sound(12000, 0, 0, 0, layers, NULL) == MH_ERROR_NONE);
	CHECK(layers[0].left == 0 && layers[1].left == (double)(float).9);
	CHECK(mh_engine_sound(100000, 1, 1, 0, layers, NULL) == MH_ERROR_NONE);
	CHECK(layers[MH_SOUND_GAS_ON_LEFT].left == 1 &&
		layers[MH_SOUND_GAS_ON_LEFT].gain == 1.8);
	CHECK(layers[MH_SOUND_GAS_ON_LEFT].pan == -1 &&
		layers[MH_SOUND_GAS_ON_RIGHT].pan == 1);
	CHECK(layers[MH_SOUND_TURBO].left == 1 &&
		layers[MH_SOUND_TURBO].gain > 4.9);
	memcpy(before, layers, sizeof(layers));
	CHECK(mh_engine_sound(-1000001, 0, 0, 0, layers, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(layers, before, sizeof(layers)) == 0);
}

static void
music_bus(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_audio_pcm sample;
	struct mh_mixer mixer, before;
	float out[16], saved[16];
	unsigned int i;

	wave_fixture(data);
	CHECK(mh_audio_wave_decode(data, WAV_BYTES, &sample, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_init(8000, .5, &mixer, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(&mixer, 0, &sample, 1, 1, 1, 0, 4, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 16; i++)
		out[i] = .125f;

	before = mixer;
	memcpy(saved, out, sizeof(out));
	CHECK(mh_mixer_add(&mixer, out, 8, 1, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&before, &mixer, sizeof(mixer)) == 0 &&
		memcmp(saved, out, sizeof(out)) == 0);
	CHECK(mh_mixer_add(&mixer, out, 8193, 0, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&before, &mixer, sizeof(mixer)) == 0 &&
		memcmp(saved, out, sizeof(out)) == 0);
	CHECK(mh_mixer_add(&mixer, out, 8, 0, NULL) == MH_ERROR_NONE);
	CHECK(out[0] == .125f && out[2] == .375f && out[4] == -.375f);
	CHECK(out[8] == .125f && out[10] == .375f &&
		mixer.voices[0].cursor == 0);

	/*
	 * Independent music volume, clipping, and phase surviving silent gain.
	 */
	mixer.gain = 1;

	for (i = 0; i < 16; i++)
		out[i] = .75f;

	CHECK(mh_mixer_add(&mixer, out, 2, 0, NULL) == MH_ERROR_NONE &&
		out[2] == 1);
	mixer.gain = 0;
	CHECK(mh_mixer_add(&mixer, out, 1, 0, NULL) == MH_ERROR_NONE &&
		out[0] == .75f && mixer.voices[0].cursor == 3);
}

static void
parse_race_bank(
	struct mh_sound_bank *bank, const unsigned char data[WAV_STORAGE_BYTES])
{
	unsigned int i;

	for (i = 0; i < MH_RACE_SAMPLES; i++)
		CHECK(mh_audio_wave_decode(data, WAV_BYTES, &bank->waves[i],
			NULL) == MH_ERROR_NONE);
}

static void
network_engine_cues(struct mh_race_sound *sound, struct mh_sound_bank *bank,
	struct mh_field *field)
{
	unsigned int i;

	/*
	 * Decoded network RPM is extrapolated, including outside the wire
	 * range. Preserve its original logical pitch; the device clamps
	 * frequency.
	 */
	field->cars[0].race.finished = 0;
	field->count = 2;
	field->network = 1;
	field->active_mask = 3;
	field->cars[1].remote = 1;

	for (i = 0; i < 2; i++)
		field->cars[i].vehicle.motion.rotation[0] =
			field->cars[i].vehicle.motion.rotation[4] =
				field->cars[i].vehicle.motion.rotation[8] = 1;

	field->cars[0].target[2] = 1;
	field->cars[1].vehicle.motion.position[2] = 1;
	field->cars[0].camera.rotation[0] = field->cars[0].camera.rotation[4] =
		field->cars[0].camera.rotation[8] = 1;
	field->cars[1].vehicle.engine.rpm = -100000;
	CHECK(mh_race_sound_update(sound, bank, field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound->mixer.voices[8].active &&
		sound->mixer.voices[8].pitch ==
			(double)(float)(.31746 - 100000.0L * .00016512));
	CHECK(field->cars[1].vehicle.engine.rpm == -100000);
	field->cars[1].vehicle.engine.rpm = 1000000;
	CHECK(mh_race_sound_update(sound, bank, field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound->mixer.voices[8].pitch ==
			(double)(float)(.31746 + 1000000.0L * .00016512));
}

static void
unavailable_race_cues(struct mh_race_sound *sound, struct mh_sound_bank *bank,
	struct mh_field *field)
{
	struct mh_race_sound before;
	float out[16];
	unsigned int i;

	/*
	 * Scene unavailable stops engines/gear, but horn processing precedes
	 * that branch and race announcements are separate sound records.
	 */
	bank->horns[0].wave = bank->waves[0];
	bank->horns[0].volume = bank->horns[0].pitch = 1;
	bank->horns[1] = bank->horns[0];
	field->cars[0].horn = field->cars[1].horn = 1;
	field->unavailable_mask = 3;
	field->cars[0].vehicle.engine.gear++;
	CHECK(mh_mixer_play(&sound->mixer, 17, &bank->waves[MH_SOUND_GEAR], 1,
		1, 1, 0, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_update(sound, bank, field, 1, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < MH_ENGINE_SAMPLES; i++)
		CHECK(!sound->mixer.voices[i].active);

	CHECK(!sound->mixer.voices[8].active &&
		!sound->mixer.voices[17].active);
	CHECK(sound->mixer.voices[20].active && sound->mixer.voices[21].active);
	field->unavailable_mask = 0;
	CHECK(mh_race_sound_update(sound, bank, field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound->mixer.voices[0].active && sound->mixer.voices[8].active &&
		!sound->mixer.voices[17].active);
	field->active_mask = 1;
	CHECK(mh_race_sound_update(sound, bank, field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound->mixer.voices[8].active);
	field->race_notice = MH_RACE_NOTICE_OUT;
	field->race_notice_revision++;
	field->unavailable_mask = 1;
	CHECK(mh_race_sound_update(sound, bank, field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound->mixer.voices[29].active && sound->mixer.voices[30].active);
	CHECK(sound->mixer.voices[29].wave ==
		&bank->waves[MH_SOUND_ELIMINATED]);
	CHECK(sound->mixer.voices[29].pitch == (double)(float).9 &&
		sound->mixer.voices[30].pitch == 1);
	CHECK(mh_mixer_render(&sound->mixer, out, 1, 0, NULL) == MH_ERROR_NONE);
	before = *sound;
	field->race_notice_revision++;
	CHECK(mh_race_sound_update(sound, bank, field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound->mixer.voices[29].cursor == before.mixer.voices[29].cursor);
	CHECK(mh_mixer_render(&sound->mixer, out, 8, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_update(sound, bank, field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound->mixer.voices[29].active);
	field->race_notice = MH_RACE_NOTICE_WIN;
	field->race_notice_revision++;
	CHECK(mh_race_sound_update(sound, bank, field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound->mixer.voices[18].active && sound->mixer.voices[28].active);
}

static void
race_cues(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_sound_bank bank;
	struct mh_race_sound sound, before;
	static struct mh_field field;
	float out[16];

	wave_fixture(data);
	memset(&bank, 0, sizeof(bank));
	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	field.count = 1;
	field.cars[0].countdown = 1;
	field.cars[0].vehicle.engine.rpm = 1000;

	parse_race_bank(&bank, data);

	CHECK(mh_race_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[16].active);
	field.cars[0].start_ticks = 720;
	before = sound;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 1, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[16].active);
	CHECK(sound.mixer.voices[16].wave == &bank.waves[MH_SOUND_COUNT]);
	CHECK(mh_mixer_render(&sound.mixer, out, 8, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[16].active);
	field.cars[0].start_ticks = 1440;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[MH_COUNTDOWN_GO].wave ==
		&bank.waves[MH_SOUND_GO]);
	field.cars[0].race.finished = 1;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[18].active && sound.mixer.voices[28].active);
	CHECK(mh_mixer_render(&sound.mixer, out, 8, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[18].active);
	CHECK(mh_race_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE &&
		!sound.finish_latch);

	network_engine_cues(&sound, &bank, &field);
	unavailable_race_cues(&sound, &bank, &field);
}

static void
viewed_engines(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_sound_bank bank;
	struct mh_race_sound sound, before;
	struct mh_sound_parameters expected[MH_ENGINE_SAMPLES];
	static struct mh_field field;
	float output[2];
	unsigned int i;

	wave_fixture(data);
	memset(&bank, 0, sizeof(bank));
	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	field.count = 2;
	field.active_mask = 3;

	parse_race_bank(&bank, data);

	for (i = 0; i < 2; i++) {
		bank.horns[i].wave = bank.waves[0];
		bank.horns[i].volume = .5;
		bank.horns[i].pitch = 1;
		field.cars[i].target[2] = 1;
		field.cars[i].horn = 1;
		field.cars[i].camera.rotation[0] =
			field.cars[i].camera.rotation[4] =
				field.cars[i].camera.rotation[8] = 1;
		field.cars[i].vehicle.motion.rotation[0] =
			field.cars[i].vehicle.motion.rotation[4] =
				field.cars[i].vehicle.motion.rotation[8] = 1;
	}

	field.cars[0].vehicle.engine.rpm = 1000;
	field.cars[0].vehicle.motion.position[2] = 50;
	field.cars[1].vehicle.engine.rpm = 9000;
	field.cars[1].sound_pedal = 0;
	field.cars[1].vehicle.motion.position[2] = 1;
	CHECK(mh_race_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_field_select_view(&field, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_engine_sound(9000, 0, 0, 0, expected, NULL) == MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].pitch == expected[0].pitch);
	CHECK(sound.mixer.voices[0].left == expected[0].left);
	CHECK(sound.mixer.voices[1].active && sound.mixer.voices[15].active);

	/* No duplicate enemy engine for the selected car. */
	CHECK(!sound.mixer.voices[8].active);
	CHECK(sound.mixer.voices[21].left == .5 &&
		sound.mixer.voices[21].right == .5);
	CHECK(sound.mixer.voices[20].left < .5 &&
		sound.mixer.voices[20].right < .5);
	CHECK(mh_mixer_render(&sound.mixer, output, 1, 0, NULL) ==
		MH_ERROR_NONE);
	before = sound;

	/* Network state synchronization. */
	CHECK(mh_field_select_view(&field, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].cursor == before.mixer.voices[0].cursor);
	CHECK(sound.mixer.voices[21].cursor == before.mixer.voices[21].cursor);

	/*
	 * A presentation-only change must not select/deselect sound or reset
	 * it.
	 */
	field.cars[1].camera_mode = MH_CAMERA_TRACK;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].cursor == before.mixer.voices[0].cursor);
	CHECK(sound.mixer.voices[21].cursor == before.mixer.voices[21].cursor);
	CHECK(sound.mixer.voices[1].active && !sound.mixer.voices[8].active);
	field.cars[0].horn = field.cars[1].horn = 0;

	/* Repeated explicit command. */
	CHECK(mh_field_camera_mode(&field, MH_CAMERA_CHASE, NULL) ==
		MH_ERROR_NONE);
	before = sound;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 1, 1, NULL) ==
		MH_ERROR_NONE);

	/* Consume only after pause. */
	CHECK(memcmp(&sound, &before, sizeof(sound)) == 0);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].cursor == 0 &&
		!sound.mixer.voices[21].active);
	CHECK(mh_field_camera_mode(&field, MH_CAMERA_TRACK, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < MH_ENGINE_SAMPLES; i++)
		CHECK(!sound.mixer.voices[i].active);

	CHECK(sound.mixer.voices[15].active && sound.mixer.voices[8].active);
	CHECK(mh_field_camera_mode(&field, MH_CAMERA_IN_CAR, NULL) ==
		MH_ERROR_NONE);
	field.cars[1].remote = 1;
	field.cars[1].vehicle.engine.rpm = 1000000;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);

	/* Playback bounds do not alter replication. */
	CHECK(field.cars[1].vehicle.engine.rpm == 1000000);
	CHECK(mh_engine_sound(1000000, 0, 0, 1, expected, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[1].pitch == expected[1].pitch);
}

static void
checkpoint_cues(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_sound_bank bank;
	struct mh_race_sound sound, saved;
	static struct mh_field field;
	float out[16];

	wave_fixture(data);
	memset(&bank, 0, sizeof(bank));
	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	CHECK(mh_audio_wave_decode(data, WAV_BYTES,
		&bank.waves[MH_SOUND_CHECKPOINT], NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE);
	field.count = 2;
	field.active_mask = 3;
	field.cars[0].race.split_hold_ms = field.cars[1].race.split_hold_ms =
		4000;
	field.cars[0].race.split_notice = 2;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.checkpoint_latch);
	saved = sound;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 1, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &saved, sizeof(sound)) == 0);
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.checkpoint_latch);
	CHECK(sound.mixer.voices[19].active &&
		sound.mixer.voices[19].wave ==
			&bank.waves[MH_SOUND_CHECKPOINT]);
	CHECK(sound.mixer.voices[19].pitch == 1 &&
		sound.mixer.voices[19].left == 1 &&
		sound.mixer.voices[19].right == 1 &&
		sound.mixer.voices[19].loop_end == 0);
	CHECK(mh_mixer_render(&sound.mixer, out, 1, 0, NULL) == MH_ERROR_NONE &&
		sound.mixer.voices[19].cursor == 1);
	field.cars[0].race.split_notice = 3;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[19].cursor == 1);

	/* A different notice/car does not re-arm the shared latch. */
	field.viewed_entry = 1;
	field.cars[1].race.split_notice = 1;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[19].cursor == 1);
	field.cars[1].race.split_notice = 0;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.checkpoint_latch);
	field.cars[1].race.split_hold_ms = 0;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.checkpoint_latch);
	field.cars[1].race.split_hold_ms = 4000;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.checkpoint_latch);
	field.cars[1].race.split_notice = 2;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.checkpoint_latch);

	/* Non-retriggering sample, even after re-arm. */
	CHECK(sound.mixer.voices[19].cursor == 1);
	CHECK(mh_mixer_render(&sound.mixer, out, 8, 0, NULL) == MH_ERROR_NONE &&
		!sound.mixer.voices[19].active);
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[19].active);
	field.cars[1].race.split_notice = 0;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) ==
		MH_ERROR_NONE);
	field.cars[1].race.split_notice = 1;
	field.cars[1].race.finished = 1;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[19].active);

	/*
	 * Listener stop clears the sample but must not re-arm the presentation
	 * latch.
	 */
	mh_mixer_stop(&sound.mixer, 19);
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[19].active);
	saved = sound;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 3, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &saved, sizeof(sound)) == 0);
	field.cars[1].race.split_notice = 4;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &saved, sizeof(sound)) == 0);
	CHECK(mh_race_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE &&
		!sound.checkpoint_latch);
	field.cars[1].race.split_notice = 2;
	bank.waves[MH_SOUND_CHECKPOINT].pcm = NULL;
	saved = sound;
	CHECK(mh_race_sound_checkpoint(&sound, &bank, &field, 2, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &saved, sizeof(sound)) == 0);
}

static void
race_sound_setup(struct mh_sound_bank *bank, struct mh_field *field,
	struct mh_race_sound *sound, unsigned char data[WAV_STORAGE_BYTES])
{
	unsigned int i;

	wave_fixture(data);
	memset(bank, 0, sizeof(*bank));
	memset(field, 0, sizeof(*field));
	field->count = 8;
	field->active_mask = 255;
	field->listener_mask = 1;

	parse_race_bank(bank, data);

	for (i = 0; i < 8; i++) {
		field->cars[i].vehicle.engine.rpm = 1000;
		field->cars[i].vehicle.motion.rotation[0] =
			field->cars[i].vehicle.motion.rotation[4] =
				field->cars[i].vehicle.motion.rotation[8] = 1;
		field->cars[i].config.options.manual = 1;
		field->cars[i].target[2] = 1;
		field->cars[i].camera.rotation[0] =
			field->cars[i].camera.rotation[4] =
				field->cars[i].camera.rotation[8] = 1;
	}

	CHECK(mh_race_sound_init(8000, 1, sound, NULL) == MH_ERROR_NONE);
}

static void
engine_environment(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_sound_bank bank;
	struct mh_race_sound sound, saved;
	static struct mh_field field;
	struct mh_route route;
	struct mh_scene_key keys[5];
	struct mh_drive *viewed;
	struct mh_mixer_voice *voices;
	unsigned int i;
	float samples[2];

	race_sound_setup(&bank, &field, &sound, data);
	memset(&route, 0, sizeof(route));
	memset(keys, 0, sizeof(keys));
	route.motion.keys = keys;
	route.motion.key_count = 5;
	route.motion.end_behavior = 2;

	for (i = 0; i < 5; i++) {
		keys[i].time = i * 10;
		keys[i].linear = 0;
	}

	keys[1].value[2] = keys[2].value[2] = 10;
	keys[2].value[0] = keys[3].value[0] = 10;
	keys[1].value[3] = 1;
	keys[2].value[3] = 2;
	keys[3].value[3] = -.5;
	field.viewed_entry = 1;
	field.listener_mask = 2;
	viewed = &field.cars[1];
	viewed->route = &route;
	viewed->sound_pedal = 1;
	viewed->vehicle.motion.position[0] = -1;
	viewed->vehicle.motion.position[2] = 2.5;
	viewed->camera_mode = MH_CAMERA_CHASE;
	voices = sound.mixer.voices;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(voices[0].left - .75) < 1e-12);
	CHECK(fabs(voices[2].left - .45) < 1e-12 && voices[2].right == 0);
	CHECK(voices[3].left == 0 && fabs(voices[3].right - .45) < 1e-12 &&
		!voices[6].active);

	/* A tilted car's display origin moves along its local up axis. */
	viewed->vehicle.motion.rotation[4] =
		viewed->vehicle.motion.rotation[8] = 0;
	viewed->vehicle.motion.rotation[5] = 1;
	viewed->vehicle.motion.rotation[7] = -1;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(voices[0].left - (.75 - (double)(float).32 / 10)) < 1e-7);
	viewed->vehicle.motion.rotation[4] =
		viewed->vehicle.motion.rotation[8] = 1;
	viewed->vehicle.motion.rotation[5] =
		viewed->vehicle.motion.rotation[7] = 0;
	CHECK(mh_mixer_render(&sound.mixer, samples, 1, 0, NULL) ==
		MH_ERROR_NONE);
	saved = sound;
	viewed->camera_mode = MH_CAMERA_IN_CAR;
	viewed->look_back = 1;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0);
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(voices[0].cursor == saved.mixer.voices[0].cursor);
	CHECK(fabs(voices[0].left - .675) < 1e-12 &&
		fabs(voices[6].left - .23125) < 1e-12);

	/*
	 * Automatic external presentation changes gain without a listener stop.
	 */
	viewed->camera_roll.external = 1;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(!voices[6].active && fabs(voices[0].left - .75) < 1e-12);
	viewed->camera_roll.external = 0;
	viewed->camera_goal = 1;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!voices[6].active);
	viewed->camera_goal = 0;
	viewed->cheats = MH_CHEAT_IGNITION;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!voices[6].active);
	viewed->cheats = MH_CHEAT_SUPERCARS;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!voices[6].active);
	viewed->cheats = 0;
	viewed->camera_mode = MH_CAMERA_CHASE;

	/*
	 * Use a remote car's live pose even when its retained race parameter
	 * and clock are frozen. Clamp the raw channel only at the mixer
	 * boundary.
	 */
	viewed->remote = 1;
	viewed->race.finished = 1;
	viewed->race_frozen = 1;
	viewed->vehicle.motion.position[0] = 10;
	viewed->vehicle.motion.position[2] = 10;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(!voices[0].active && voices[2].left == 1 && voices[3].right == 1);

	/* Final omitted segment holds key 3, instead of blending to key 0. */
	viewed->vehicle.motion.position[0] = 5;
	viewed->vehicle.motion.position[2] = 0;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(voices[0].left == 1 && !voices[2].active && !voices[3].active);

	/*
	 * A real race publishes its environment once during progress. Even a
	 * moved/eliminated car consumes that retained value, without searching
	 * its pose again. Preserve raw values until this final audio clamp.
	 */
	viewed->race.route = &route;
	viewed->race.sound_blend = .25f;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(voices[0].left == .75 && fabs(voices[2].left - .45) < 1e-12);
	viewed->race.sound_blend = 2;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(!voices[0].active && voices[2].left == 1 &&
		viewed->race.sound_blend == 2);
	viewed->race.sound_blend = -.5f;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(voices[0].left == 1 && !voices[2].active &&
		viewed->race.sound_blend == -.5f);
	viewed->race.route = NULL;
	saved = sound;
	field.listener_revision++;
	route.motion.key_count = 0;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0);

	/*
	 * Positional-only and unavailable playback do not evaluate this route.
	 */
	field.listener_mask = 0;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(!voices[0].active && voices[8].active);
	field.listener_mask = 1U << field.viewed_entry;
	field.unavailable_mask = 2;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!voices[0].active);
}

static void
sound_phases(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_sound_bank bank;
	struct mh_race_sound sound, saved;
	static struct mh_field field;
	double pitch;

	race_sound_setup(&bank, &field, &sound, data);
	field.cars[0].countdown = 1;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	pitch = sound.mixer.voices[0].pitch;

	/* Physics publishes new RPM and HUD time between the two consumers. */
	field.cars[0].vehicle.engine.rpm = 9000;
	field.cars[0].start_ticks = 720;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].pitch == pitch &&
		sound.mixer.voices[16].active);
	field.cars[0].race.finished = 1;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].pitch != pitch &&
		!sound.mixer.voices[18].active);
	saved = sound;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 1, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &saved, sizeof(sound)) == 0);
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[18].active);
	CHECK(!sound.mixer.voices[0].active && sound.finish_latch == 1);
}

static void
countdown_cues(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_sound_bank bank;
	struct mh_race_sound sound, saved;
	struct mh_sound_pool pool, saved_pool;
	static struct mh_field field;
	struct mh_drive *drive;
	unsigned int i;
	static const unsigned int voices[4] = {MH_COUNTDOWN_THREE,
		MH_COUNTDOWN_TWO, MH_COUNTDOWN_ONE, MH_COUNTDOWN_GO};

	race_sound_setup(&bank, &field, &sound, data);
	drive = &field.cars[0];
	drive->countdown = drive->frame.active = 1;
	drive->frame.intro_ms = 1000;
	CHECK(mh_sound_pool_init(MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_bind(&sound.mixer, &pool, 0, NULL) == MH_ERROR_NONE);

	/*
	 * Start mid-countdown, then jump between windows without rendering PCM:
	 * all four records coexist, even though the first three share a WAV.
	 */
	for (i = 0; i < 4; i++) {
		drive->frame.time.cue = 3 - (int)i;
		CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0,
			NULL) == MH_ERROR_NONE);
		CHECK(sound.countdown[i] == 1 &&
			sound.mixer.voices[voices[i]].active);
		CHECK(sound.mixer.flags[voices[i]] == 0 &&
			sound.mixer.priority[voices[i]] == 3);
		sound.mixer.voices[voices[i]].cursor = .5;
	}

	for (i = 0; i < 4; i++)
		CHECK(sound.mixer.voices[voices[i]].active);

	drive->frame.time.cue = 3;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[MH_COUNTDOWN_THREE].cursor == .5);
	saved = sound;
	saved_pool = pool;
	drive->frame.time.cue = 2;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 1, NULL) ==
		MH_ERROR_NONE);
	drive->frame.intro_ms = 0;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0 &&
		memcmp(&pool, &saved_pool, sizeof(pool)) == 0);
	drive->frame.intro_ms = 1000;
	drive->frame.time.cue = -1;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 4; i++)
		CHECK(sound.countdown[i] == 0 &&
			sound.mixer.voices[voices[i]].active);

	mh_mixer_stop(&sound.mixer, MH_COUNTDOWN_GO);
	drive->frame.time.cue = 0;
	bank.waves[MH_SOUND_GO].pcm = NULL;
	saved = sound;
	saved_pool = pool;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0 &&
		memcmp(&pool, &saved_pool, sizeof(pool)) == 0);
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 2, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0 &&
		memcmp(&pool, &saved_pool, sizeof(pool)) == 0);
}

static void
finish_cues(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_sound_bank bank;
	struct mh_race_sound sound, saved;
	struct mh_sound_pool pool, saved_pool;
	static struct mh_field field;
	int pending;

	race_sound_setup(&bank, &field, &sound, data);
	field.count = 2;
	field.active_mask = 3;
	field.viewed_entry = 1;
	field.cars[1].race.finished = 1;
	field.listener_revision = 9;
	CHECK(mh_sound_pool_init(MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_bind(&sound.mixer, &pool, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_message(&sound, &bank, 1, 0, NULL) ==
		MH_ERROR_NONE);
	saved = sound;
	saved_pool = pool;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 1, 1, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0 &&
		memcmp(&pool, &saved_pool, sizeof(pool)) == 0);

	/* Failure must undo the global stop within this bank as well. */
	bank.waves[MH_SOUND_FINISH].pcm = NULL;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 1, 1, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0 &&
		memcmp(&pool, &saved_pool, sizeof(pool)) == 0);
	bank.waves[MH_SOUND_FINISH] = bank.waves[MH_SOUND_COUNT];
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 1, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.finish_latch == 1);
	CHECK(!sound.mixer.voices[31].active && sound.mixer.voices[18].active &&
		sound.mixer.voices[28].active);
	CHECK(pool.pending[0] == 19 && pool.pending[1] == 29 &&
		pool.pending[2] == 0);
	field.listener_mask = 0;
	CHECK(mh_race_sound_vehicles(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.listener_revision == 9 && sound.mixer.voices[18].active);
	sound.mixer.voices[18].cursor = .5;
	field.cars[0].race.finished = 1;
	field.viewed_entry = 0;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[18].cursor == .5);
	field.cars[0].race.finished = 0;
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 1, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.finish_latch);
	field.viewed_entry = 1;
	CHECK(mh_race_sound_finish_pending(&sound, &field, &pending, NULL) ==
			MH_ERROR_NONE &&
		pending);
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 1, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[18].cursor == 0);
	sound.finish_latch = 0;
	CHECK(mh_race_sound_cues(&sound, NULL, &field, 1, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.finish_latch == 1);

	/* Missing native PCM still advances the HUD latch. */
	CHECK(!sound.mixer.voices[18].active);
	field.cars[1].race.finished = 2;
	saved = sound;
	pending = 5;
	CHECK(mh_race_sound_finish_pending(&sound, &field, &pending, NULL) !=
			MH_ERROR_NONE &&
		pending == 5);
	CHECK(mh_race_sound_cues(&sound, &bank, &field, 0, 0, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &saved, sizeof(sound)) == 0);
}

static void
gear_cues(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	struct mh_sound_bank bank;
	struct mh_race_sound sound, saved;
	static struct mh_field field;
	float samples[4];

	race_sound_setup(&bank, &field, &sound, data);
	field.cars[0].vehicle.engine.gear = 1;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 1 && sound.mixer.voices[17].active);
	CHECK(mh_mixer_render(&sound.mixer, samples, 1, 0, NULL) ==
		MH_ERROR_NONE);
	field.cars[0].vehicle.engine.gear = 2;
	field.listener_mask = 0;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 1 && sound.mixer.voices[17].active &&
		sound.mixer.voices[17].cursor == 1);
	field.listener_mask = 1U << field.viewed_entry;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 2 && sound.mixer.voices[17].cursor == 1);
	mh_mixer_stop(&sound.mixer, 17);
	field.cars[0].config.options.manual = 0;
	field.cars[0].vehicle.engine.gear = 3;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 2 && !sound.mixer.voices[17].active);
	field.cars[0].config.options.manual = 1;
	field.unavailable_mask = 1;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 2 && !sound.mixer.voices[17].active);
	field.unavailable_mask = 0;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 3 && sound.mixer.voices[17].active);
	CHECK(mh_field_camera_mode(&field, MH_CAMERA_TRACK, NULL) ==
		MH_ERROR_NONE);
	field.cars[0].vehicle.engine.gear = 4;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 3 && !sound.mixer.voices[17].active);
	CHECK(mh_field_camera_mode(&field, MH_CAMERA_CHASE, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 4 && sound.mixer.voices[17].active);
	field.cars[1].vehicle.engine.gear = 5;
	field.cars[1].remote = 1;
	CHECK(mh_field_select_view(&field, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 4 && sound.gear[1] == 5 &&
		sound.mixer.voices[17].active);
	CHECK(mh_field_select_view(&field, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(!sound.mixer.voices[17].active && sound.gear[0] == 4);
	field.cars[0].vehicle.engine.gear = 0;
	saved = sound;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &sound, sizeof(sound)) == 0);
	bank.waves[MH_SOUND_GEAR].pcm = NULL;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &sound, sizeof(sound)) == 0);
	bank.waves[MH_SOUND_GEAR] = bank.waves[0];
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.gear[0] == 0 && sound.mixer.voices[17].active);
	CHECK(mh_race_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(sound.gear[0] == 0 && sound.gear[1] == 0 &&
		!sound.mixer.voices[17].active);
}

static void
message_cue(void)
{
	unsigned char data[WAV_STORAGE_BYTES];
	float output[32];
	struct mh_race_sound sound, before;
	struct mh_sound_bank bank;

	wave_fixture(data);
	memset(&bank, 0, sizeof(bank));
	CHECK(mh_audio_wave_decode(data, WAV_BYTES,
		&bank.waves[MH_SOUND_MESSAGE], NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE);
	before = sound;
	CHECK(mh_race_sound_message(&sound, &bank, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
	CHECK(mh_race_sound_message(&sound, &bank, 1, 1, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
	CHECK(mh_race_sound_message(&sound, &bank, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[31].active &&
		sound.mixer.priority[31] == 3 && sound.mixer.flags[31] == 0);
	CHECK(mh_mixer_render(&sound.mixer, output, 1, 0, NULL) ==
		MH_ERROR_NONE);
	before = sound;
	CHECK(mh_race_sound_message(&sound, &bank, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
	CHECK(mh_mixer_render(&sound.mixer, output, 16, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[31].active);
	CHECK(mh_race_sound_message(&sound, &bank, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[31].active &&
		sound.mixer.voices[31].cursor == 0);
	before = sound;
	CHECK(mh_race_sound_message(&sound, &bank, 2, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
}

int
main(void)
{

	wave();
	mixing();
	engine();
	race_cues();
	music_bus();
	viewed_engines();
	checkpoint_cues();
	gear_cues();
	engine_environment();
	message_cue();
	sound_phases();
	countdown_cues();
	finish_cues();

	return failures != 0;
}
