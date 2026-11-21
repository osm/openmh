#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/audio/wave_provider.h"
#include "backends/material/provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/audio/menu_playback.h"
#include "engine/audio/race_playback.h"
#include "engine/core/binary.h"
#include "engine/ui/pause.h"
#include "interfaces/audio/backend.h"
#include "interfaces/catalog/track.h"
#include "interfaces/clock/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "audio provider line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

#define TEST_FRAMES 38400
#define WAVE_FRAMES 22050

/* A host-owned callback queue and PCM sink: no SDK, threads or OS clock. */
struct fixture {
	struct mh_audio_callbacks	callbacks;
	struct mh_audio_format		format;
	size_t				frame_bytes;
	int				flushed, tail_pending;
	float				samples[TEST_FRAMES * 2];
	size_t				frames;
	unsigned long			calls;
	unsigned int			open_attempts, opens, closes, clears;
	int				fail_open, fail_handoff;
	int	active, started, locked, fail_start, fail_submit, failed, paused;
};

static enum mh_error_code
open_device(void *context, const struct mh_audio_format *format,
	const struct mh_audio_callbacks *callbacks, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;
	fixture->open_attempts++;

	if (fixture->fail_open)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "host audio unavailable");

	if (fixture->active)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "fixture occupied");

	fixture->format = *format;
	fixture->frame_bytes = format->channels *
		(format->encoding == MH_AUDIO_NATIVE_FLOAT ? sizeof(float) : 2);
	fixture->callbacks = *callbacks;
	fixture->active = 1;
	fixture->opens++;
	*output = fixture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_device(void *context, void *token)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;
	fixture->active = fixture->started = 0;
	memset(&fixture->callbacks, 0, sizeof(fixture->callbacks));
	fixture->closes++;
}

static enum mh_error_code
start_device(void *context, void *token, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;

	if (fixture->fail_start)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"host audio activation denied");

	fixture->started = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
lock_device(void *context, void *token, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;

	if (fixture->locked)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "recursive exclusion");

	fixture->locked = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
unlock_device(void *context, void *token, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;

	if (!fixture->locked)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "missing exclusion");

	fixture->locked = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
clear_device(void *context, void *token, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;

	if (!fixture->locked)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "unprotected clear");

	fixture->clears++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
submit_output(void *context, void *token, const void *samples, size_t size,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	size_t frames;

	(void)context;
	fixture = token;
	frames = size / fixture->frame_bytes;

	if (!fixture->locked || fixture->fail_submit ||
		frames > TEST_FRAMES - fixture->frames)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "output rejected");

	memcpy((unsigned char *)fixture->samples +
			fixture->frames * fixture->frame_bytes,
		samples, size);
	fixture->frames += frames;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
inspect_device(void *context, void *token, struct mh_audio_device_state *state,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;

	if (!fixture->locked)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "unprotected snapshot");

	state->service_calls = fixture->calls;
	state->failed = fixture->failed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
pause_device(void *context, void *token, int paused, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;

	if (fixture->locked)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "pause under exclusion");

	fixture->paused = paused;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
stop_service(void *context, void *token, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;

	if (fixture->locked)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "stop under exclusion");

	fixture->callbacks.service = NULL;
	fixture->calls = 0;
	fixture->failed = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_request(void *context, void *token, void (*request)(void *, size_t),
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;

	if (!fixture->locked)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "unprotected handoff");

	if (fixture->fail_handoff)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"host callback replacement rejected");

	fixture->callbacks.request = request;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
flush_output(void *context, void *token, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;
	fixture->flushed = 1;
	fixture->tail_pending = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
drained_output(
	void *context, void *token, int *drained, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;
	*drained = fixture->flushed && fixture->frames == 0 &&
		!fixture->tail_pending;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_audio_device_ops ops = {open_device, close_device,
	start_device, lock_device, unlock_device, clear_device, submit_output,
	inspect_device, pause_device, stop_service, set_request, NULL,
	flush_output, drained_output};

static void
host_tick(struct fixture *fixture)
{
	if (!fixture->active || !fixture->started || fixture->locked ||
		fixture->failed)
		return;

	fixture->locked = 1;

	if (fixture->callbacks.service != NULL) {
		fixture->calls++;
		fixture->failed =
			fixture->callbacks.service(fixture->callbacks.context,
				NULL) != MH_ERROR_NONE;
	}

	if (!fixture->failed && !fixture->paused &&
		fixture->callbacks.request != NULL)
		fixture->callbacks.request(fixture->callbacks.context, 1920);

	fixture->locked = 0;
}

static unsigned char *
make_wave(void)
{
	unsigned char *wave;
	unsigned int i;

	wave = calloc(1, 44 + WAVE_FRAMES * 2);

	if (wave == NULL)
		return NULL;

	memcpy(wave, "RIFF", 4);
	mh_write_u32le(wave + 4, 36 + WAVE_FRAMES * 2);
	memcpy(wave + 8, "WAVEfmt ", 8);
	mh_write_u32le(wave + 16, 16);
	wave[20] = 1;
	wave[22] = 1;
	mh_write_u32le(wave + 24, 22050);
	mh_write_u32le(wave + 28, 44100);
	wave[32] = 2;
	wave[34] = 16;
	memcpy(wave + 36, "data", 4);
	mh_write_u32le(wave + 40, WAVE_FRAMES * 2);

	for (i = 0; i < WAVE_FRAMES; i++)
		wave[44 + i * 2 + 1] = 64;

	return wave;
}

static struct mh_clock_counter
clock_counter(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = 1000;
	value.high = 0;

	return value;
}

static unsigned long
clock_ticks(void *context)
{
	(void)context;

	return 1000;
}

static int
race_start_failures(const struct mh_audio_provider *provider,
	struct fixture *fixture, const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, const struct mh_track *track,
	const struct mh_audio_options *options)
{
	unsigned int attempts, opens, closes;

	attempts = fixture->open_attempts;
	opens = fixture->opens;
	closes = fixture->closes;
	CHECK(mh_audio_open_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_animation_lightwave_source(), NULL,
		mh_audio_wave_decoder(), "game", track, NULL, options,
		resources, clock, 0) == NULL);
	CHECK(fixture->open_attempts == attempts);
	fixture->fail_open = 1;
	CHECK(mh_audio_open_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_animation_lightwave_source(), provider,
		mh_audio_wave_decoder(), "game", track, NULL, options,
		resources, clock, 0) == NULL);
	CHECK(fixture->open_attempts == attempts + 1 &&
		fixture->opens == opens && fixture->closes == closes);
	CHECK(!fixture->active && fixture->callbacks.context == NULL);
	fixture->fail_open = 0;

	/* Start failure closes the acquired device, without a profile retry. */
	fixture->fail_start = 1;
	CHECK(mh_audio_open_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_animation_lightwave_source(), provider,
		mh_audio_wave_decoder(), "game", track, NULL, options,
		resources, clock, 0) == NULL);
	CHECK(fixture->open_attempts == attempts + 2 &&
		fixture->opens == opens + 1 && fixture->closes == closes + 1);
	CHECK(!fixture->active && fixture->callbacks.context == NULL);
	fixture->fail_start = 0;

	return 0;
}

static int
race_playback(const struct mh_audio_provider *provider, struct fixture *fixture,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, int spatial)
{
	struct mh_track track;
	struct mh_audio_options options;
	struct mh_audio_music_state before, after;
	struct mh_pause_output pause;
	struct mh_audio *race;
	unsigned int clears;
	size_t frames;

	memset(&track, 0, sizeof(track));
	memset(&options, 0, sizeof(options));
	memset(&pause, 0, sizeof(pause));
	track.cd_track = 2;
	options.music_root = "audio";
	options.spatial = spatial;
	options.effects_volume = options.music_volume = 1;
	race = mh_audio_open_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_animation_lightwave_source(), provider,
		mh_audio_wave_decoder(), "game", &track, NULL, &options,
		resources, clock, 0);
	CHECK(race != NULL);
	CHECK(mh_audio_music_inspect(race, &before) && before.track == 2 &&
		before.started_ms == 1000 && before.frame == 0);
	CHECK(!mh_audio_continue_results(race));
	pause.count = 1;
	pause.events[0].kind = MH_PAUSE_PLAY;
	pause.events[0].value = MH_PAUSE_LOOP;
	CHECK(mh_audio_menu(race, &pause, 1));
	host_tick(fixture);
	host_tick(fixture);
	CHECK(mh_audio_music_inspect(race, &before));
	CHECK(before.frame > 0 && before.effects_channels == 1 &&
		before.effects_callbacks == 2 && !before.failed);

	if (spatial == MH_RACE_AUDIO_SOFTWARE)
		CHECK(before.effects_fills > 0 && before.effects_frames > 0);
	else
		CHECK(before.effects_fills == 0 && before.effects_frames == 0);
	CHECK(mh_audio_frame_begin(race, NULL, 0, 1, 0));
	frames = fixture->frames;
	host_tick(fixture);
	CHECK(mh_audio_music_inspect(race, &after));
	CHECK(after.frame == before.frame && fixture->frames == frames &&
		after.effects_callbacks == 3);
	CHECK(mh_audio_frame_begin(race, NULL, 0, 0, 0));
	host_tick(fixture);
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.frame > before.frame);
	CHECK(mh_audio_results(race, 0));
	CHECK(mh_audio_music_inspect(race, &before));
	CHECK(before.effects_active == 0 && before.effects_channels == 0 &&
		before.effects_pending == 0);
	clears = fixture->clears;

	/* A rejected handoff must leave the old callback's state alive. */
	fixture->fail_handoff = 1;
	CHECK(!mh_audio_continue_results(race));
	CHECK(!fixture->locked && fixture->callbacks.service == NULL);
	CHECK(mh_audio_music_inspect(race, &after) && !after.background);
	host_tick(fixture);
	CHECK(mh_audio_music_inspect(race, &after) && !after.failed &&
		after.frame > before.frame);
	before = after;
	fixture->fail_handoff = 0;
	CHECK(mh_audio_continue_results(race));
	CHECK(fixture->callbacks.service == NULL && fixture->clears == clears);
	CHECK(mh_audio_music_inspect(race, &after) && after.background &&
		after.frame == before.frame && after.effects_callbacks == 0);
	CHECK(after.effects_fills == before.effects_fills &&
		after.effects_wraps == before.effects_wraps &&
		after.effects_frames == before.effects_frames);
	host_tick(fixture);
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.frame > before.frame && after.callbacks == 1 &&
		!after.failed);
	CHECK(mh_audio_frame_begin(race, NULL, 0, 1, 1));
	frames = fixture->frames;
	host_tick(fixture);
	CHECK(fixture->frames == frames);
	CHECK(mh_audio_frame_begin(race, NULL, 0, 0, 1));
	mh_audio_close(race);
	CHECK(!fixture->active && fixture->callbacks.context == NULL);

	CHECK(race_start_failures(provider, fixture, resources, clock, &track,
		&options) == 0);

	/*
	 * Resumed results start with no effects service or native queue query.
	 */
	race = mh_audio_open_results_from(provider, mh_audio_wave_decoder(),
		"game", "audio", 255, resources, clock, 0);
	CHECK(race != NULL && fixture->callbacks.service == NULL);
	host_tick(fixture);
	CHECK(mh_audio_music_inspect(race, &after) && after.track == 2 &&
		after.background && after.callbacks == 1 && after.frame > 0);
	mh_audio_close(race);
	CHECK(fixture->opens == fixture->closes && !fixture->active);

	return 0;
}

static enum mh_error_code
queued_frames(
	void *context, void *token, size_t *frames, struct mh_error *error_out)
{
	const struct fixture *fixture;

	(void)context;
	fixture = token;

	if (!fixture->locked)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"unprotected queue inspection");

	*frames = fixture->frames;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
manual_race_playback(struct fixture *fixture,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, int profile)
{
	struct mh_audio_device_ops manual_ops;
	struct mh_audio_provider provider;
	struct mh_audio_options options;
	struct mh_track track;
	struct mh_pause_output pause;
	struct mh_audio_music_state before, after;
	struct mh_audio *race;
	size_t i;
	int heard;

	memset(&options, 0, sizeof(options));
	memset(&track, 0, sizeof(track));
	memset(&pause, 0, sizeof(pause));
	manual_ops = ops;
	manual_ops.queued = queued_frames;
	CHECK(mh_audio_provider_init(&provider, &manual_ops, fixture, NULL) ==
		MH_ERROR_NONE);
	options.spatial = profile;
	options.effects_volume = 1;
	options.manual_service = 1;
	race = mh_audio_open_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_animation_lightwave_source(), &provider,
		mh_audio_wave_decoder(), "game", &track, NULL, &options,
		resources, clock, 0);
	CHECK(race != NULL && fixture->callbacks.request == NULL &&
		fixture->callbacks.service == NULL);
	pause.count = 1;
	pause.events[0].kind = MH_PAUSE_PLAY;
	pause.events[0].value = MH_PAUSE_LOOP;
	CHECK(mh_audio_menu(race, &pause, 1));
	CHECK(mh_audio_service(race));
	CHECK(fixture->frames == 2048);
	heard = 0;

	for (i = 0; i < fixture->frames * 2; i++)
		heard |= fixture->samples[i] != 0;

	CHECK(heard);
	CHECK(mh_audio_music_inspect(race, &before));
	CHECK(before.effects_channels == 1 && before.effects_callbacks == 0 &&
		before.effects_fills == 0 && before.effects_frames == 0);
	CHECK(mh_audio_service(race) && fixture->frames == 2048);
	CHECK(mh_audio_frame_begin(race, NULL, 0, 1, 0));
	fixture->frames = 0;
	CHECK(mh_audio_service(race) && fixture->frames == 0);
	CHECK(mh_audio_frame_begin(race, NULL, 0, 0, 0));
	pause.events[0].kind = MH_PAUSE_EFFECTS_VOLUME;
	pause.events[0].value = 0;
	CHECK(mh_audio_menu(race, &pause, 1));
	CHECK(mh_audio_service(race) && fixture->frames == 2048);

	/* Spatial gain changes retain their short transition ramp. */
	for (i = fixture->frames; i < fixture->frames * 2; i++)
		CHECK(fixture->samples[i] == 0);

	CHECK(mh_audio_music_inspect(race, &after) &&
		after.effects_channels == 1);
	pause.events[0].kind = MH_PAUSE_DEVICE_STOP;
	CHECK(mh_audio_menu(race, &pause, 1));
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.effects_channels == 0);
	mh_audio_reset(race);
	CHECK(mh_audio_results(race, 0));
	CHECK(mh_audio_continue_results(race));
	CHECK(mh_audio_music_inspect(race, &after) && after.background &&
		after.effects_channels == 0 && !after.failed);
	mh_audio_close(race);
	CHECK(fixture->opens == fixture->closes && !fixture->active);

	return 0;
}

static int
finite_queue(void)
{
	static const unsigned char pcm[8] = {
		0x00, 0x80, 0xff, 0x7f, 1, 0, 0xff, 0xff};

	struct fixture *fixture;
	struct mh_audio_provider provider;
	struct mh_audio_device_ops limited;
	struct mh_audio_device *device;
	struct mh_audio_format format;
	int drained;

	fixture = calloc(1, sizeof(*fixture));
	CHECK(fixture != NULL);
	format.encoding = MH_AUDIO_S16LE;
	format.rate = 44100;
	format.channels = 2;
	CHECK(mh_audio_provider_init(&provider, &ops, fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_audio_device_open_format(&provider, &format, NULL, &device,
		NULL) == MH_ERROR_NONE);

	/* The provider must have copied the caller's values. */
	format.rate = 8000;
	CHECK(fixture->format.rate == 44100 &&
		fixture->callbacks.request == NULL &&
		fixture->callbacks.service == NULL);
	CHECK(mh_audio_device_lock(device, NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_device_write(device, pcm, sizeof(pcm), NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture->frames == 2 &&
		memcmp(fixture->samples, pcm, sizeof(pcm)) == 0);
	CHECK(mh_audio_device_submit(device, fixture->samples, 1, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_audio_device_write(device, pcm, 3, NULL) == MH_ERROR_INVALID);
	CHECK(fixture->frames == 2);
	CHECK(mh_audio_device_flush(device, NULL) == MH_ERROR_NONE);
	CHECK(mh_audio_device_drained(device, &drained, NULL) ==
			MH_ERROR_NONE &&
		!drained);
	fixture->frames = 0; /* Host consumed input; conversion tail remains. */
	CHECK(mh_audio_device_drained(device, &drained, NULL) ==
			MH_ERROR_NONE &&
		!drained);
	fixture->tail_pending = 0;
	CHECK(mh_audio_device_drained(device, &drained, NULL) ==
			MH_ERROR_NONE &&
		drained);
	CHECK(mh_audio_device_unlock(device, NULL) == MH_ERROR_NONE);
	mh_audio_device_close(device);
	CHECK(fixture->opens == fixture->closes);
	limited = ops;
	limited.flush = NULL;
	limited.drained = NULL;
	CHECK(mh_audio_provider_init(&provider, &limited, fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_audio_device_open_format(&provider, &format, NULL, &device,
		NULL) == MH_ERROR_NONE);
	drained = 7;
	CHECK(mh_audio_device_flush(device, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_audio_device_drained(device, &drained, NULL) ==
			MH_ERROR_UNSUPPORTED &&
		drained == 7);
	mh_audio_device_close(device);
	CHECK(fixture->opens == fixture->closes);
	free(fixture);

	return 0;
}

static int
menu_playback(struct mh_audio_provider providers[2], struct fixture fixtures[2],
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock)
{
	struct mh_menu_audio *menus[2];
	struct mh_menu_audio_state before, after;
	unsigned int i, j;
	int heard;

	for (i = 0; i < 2; i++) {
		CHECK(mh_audio_provider_init(&providers[i], &ops, &fixtures[i],
			NULL) == MH_ERROR_NONE);
		menus[i] = mh_menu_audio_open_from(&providers[i],
			mh_audio_wave_decoder(), "game", 1, resources, clock);
		CHECK(menus[i] != NULL && fixtures[i].opens == 1);
		CHECK(fixtures[i].format.encoding == MH_AUDIO_NATIVE_FLOAT &&
			fixtures[i].format.rate == MH_AUDIO_OUTPUT_RATE &&
			fixtures[i].format.channels ==
				MH_AUDIO_OUTPUT_CHANNELS);
		CHECK(fixtures[i].frames == 0 && fixtures[i].calls == 0);
		CHECK(mh_menu_audio_play(menus[i], 1));
	}

	CHECK(mh_menu_audio_inspect(menus[1], &before));

	for (i = 0; i < 20; i++) {
		host_tick(&fixtures[0]);

		/*
		 * Arbitrary render/update frequency cannot advance sound
		 * service.
		 */
		for (j = 0; j < 37; j++)
			CHECK(mh_menu_audio_update(menus[1]));

		host_tick(&fixtures[1]);
	}

	CHECK(fixtures[0].frames == TEST_FRAMES &&
		fixtures[1].frames == TEST_FRAMES);
	CHECK(fixtures[0].calls == 20 && fixtures[1].calls == 20);
	CHECK(memcmp(fixtures[0].samples, fixtures[1].samples,
		sizeof(fixtures[0].samples)) == 0);
	heard = 0;

	for (i = 0; i < TEST_FRAMES * 2; i++)
		if (fixtures[0].samples[i] != 0)
			heard = 1;

	CHECK(heard);
	CHECK(mh_menu_audio_inspect(menus[1], &after));
	CHECK(before.effects_callbacks == 0 && before.effects_frames == 0);
	CHECK(after.effects_callbacks == 20 && after.effects_frames == 17640);
	CHECK(mh_menu_audio_effects_volume(menus[0], 0));
	CHECK(mh_menu_audio_inspect(menus[1], &after) &&
		after.effects_gain == .125);
	CHECK(mh_menu_audio_leave(menus[0]) && fixtures[0].clears == 1);
	CHECK(fixtures[1].clears == 0);

	for (i = 0; i < 2; i++) {
		mh_menu_audio_close(menus[i]);
		CHECK(fixtures[i].closes == 1 && !fixtures[i].active);
		CHECK(fixtures[i].callbacks.request == NULL &&
			fixtures[i].callbacks.service == NULL &&
			fixtures[i].callbacks.context == NULL);
		host_tick(&fixtures[i]);
		CHECK(fixtures[i].calls == 20);
	}

	fixtures[0].fail_start = 1;
	CHECK(mh_menu_audio_open_from(&providers[0], mh_audio_wave_decoder(),
		"game", 1, resources, clock) == NULL);
	CHECK(fixtures[0].opens == 2 && fixtures[0].closes == 2 &&
		!fixtures[0].active);
	fixtures[0].fail_start = 0;
	fixtures[0].fail_submit = 1;
	menus[0] = mh_menu_audio_open_from(&providers[0],
		mh_audio_wave_decoder(), "game", 1, resources, clock);
	CHECK(menus[0] != NULL);
	host_tick(&fixtures[0]);
	CHECK(!mh_menu_audio_update(menus[0]));
	mh_menu_audio_close(menus[0]);
	CHECK(fixtures[0].opens == 3 && fixtures[0].closes == 3);

	return 0;
}

int
main(void)
{
	static const unsigned char config[] =
		"SampleName tone.wav\nSampleEvent 1\n";
	struct mh_resource_entry entries[7];
	static const char *const pause_paths[4] = {"game/Sounds/menyloop.wav",
		"game/Sounds/enter.wav", "game/Sounds/cursor.wav",
		"game/Sounds/error.wav"};

	static const struct mh_clock_ops clock_ops = {
		clock_counter, clock_counter, clock_ticks, NULL, NULL};

	const struct mh_resource_source *resources;
	struct mh_clock_source clock;
	struct mh_audio_provider providers[2], invalid;
	struct mh_audio_device_ops incomplete;
	struct fixture *fixtures;
	unsigned char *wave, *music;
	unsigned int i;

	CHECK(finite_queue() == 0);
	fixtures = calloc(2, sizeof(*fixtures));
	wave = make_wave();
	CHECK(fixtures != NULL && wave != NULL);

	/*
	 * No music is selected: resource callbacks and timing are not sampled.
	 */
	CHECK(mh_clock_source_init(&clock, &clock_ops, NULL, NULL) ==
		MH_ERROR_NONE);
	entries[0].path = "game/Sounds/Menu/menusound.cfg";
	entries[0].data = config;
	entries[0].size = sizeof(config) - 1;
	entries[1].path = "game/Sounds/Menu/tone.wav";
	entries[1].data = wave;
	entries[1].size = 44 + WAVE_FRAMES * 2;

	for (i = 0; i < 4; i++) {
		entries[i + 2].path = pause_paths[i];
		entries[i + 2].data = wave;
		entries[i + 2].size = entries[1].size;
	}

	music = make_wave();
	CHECK(music != NULL);
	music[22] = 2;
	music[32] = 4;
	mh_write_u32le(music + 24, 44100);
	mh_write_u32le(music + 28, 176400);
	entries[6].path = "audio/track-02.wav";
	entries[6].data = music;
	entries[6].size = entries[1].size;
	CHECK(mh_memory_resources_open(entries, 7, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_menu_audio_open_from(NULL, mh_audio_wave_decoder(), "game", 1,
		resources, &clock) == NULL);
	incomplete = ops;
	incomplete.close = NULL;
	memset(&invalid, 0, sizeof(invalid));
	CHECK(mh_audio_provider_init(&invalid, &incomplete, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(invalid.ops == NULL);

	CHECK(menu_playback(providers, fixtures, resources, &clock) == 0);
	memset(fixtures, 0, sizeof(*fixtures));
	CHECK(race_playback(&providers[0], &fixtures[0], resources, &clock,
		MH_RACE_AUDIO_SOFTWARE) == 0);
	memset(fixtures, 0, sizeof(*fixtures));
	CHECK(race_playback(&providers[0], &fixtures[0], resources, &clock,
		MH_RACE_AUDIO_DS3D) == 0);
	memset(fixtures, 0, sizeof(*fixtures));
	CHECK(race_playback(&providers[0], &fixtures[0], resources, &clock,
		MH_RACE_AUDIO_EAX) == 0);
	memset(fixtures, 0, sizeof(*fixtures));
	CHECK(race_playback(&providers[0], &fixtures[0], resources, &clock,
		MH_RACE_AUDIO_A3D12) == 0);
	memset(fixtures, 0, sizeof(*fixtures));
	CHECK(race_playback(&providers[0], &fixtures[0], resources, &clock,
		MH_RACE_AUDIO_A3D20) == 0);

	for (i = MH_RACE_AUDIO_SOFTWARE; i <= MH_RACE_AUDIO_A3D20; i++) {
		memset(fixtures, 0, sizeof(*fixtures));
		CHECK(manual_race_playback(
			&fixtures[0], resources, &clock, (int)i) == 0);
	}

	mh_memory_resources_close(resources);
	free(music);
	free(wave);
	free(fixtures);
	puts("threadless menu/race/results audio: independent providers, PCM, exclusion and teardown pass");

	return 0;
}
