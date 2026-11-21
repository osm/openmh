#include <stdio.h>
#include <stdlib.h>

#include "engine/audio/mixer_internal.h"
#include "engine/audio/race_playback_internal.h"
#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "interfaces/clock/clock.h"

#define CD_TRACK_MASK 255

/*
 * Original crash and six material records outlive the device and every race.
 * Main-thread ownership ends at results/close; music-only continuation never
 * edits their history. No sample pointers survive here.
 */
static struct mh_crash_sound previous_crash = {-1, 0, 0, 0};
static struct mh_surface_history previous_material;

int
mh_race_audio_clear_effects(struct mh_audio *audio)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_race_profile_clear(audio->profile, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Effects clear: %s\n", error.message);

	return status == MH_ERROR_NONE;
}

static void
retain_sound_history(struct mh_audio *audio)
{
	struct mh_crash_sound next;

	if (!audio->owns_sound_history)
		return;

	next = audio->surface_sound.crash;

	if (mh_crash_sound_unload(&next,
		audio->surface_sound.mixer.voices[MH_SURFACE_VOICE_CRASH]
			.active,
		NULL) == MH_ERROR_NONE)
		previous_crash = next;

	/* Software channel aliases cannot survive a spatial device lifetime. */
	if (audio->audio_profile == MH_RACE_AUDIO_SOFTWARE)
		mh_surface_history_save(
			&audio->surface_sound, &previous_material, NULL);

	audio->owns_sound_history = 0;
}

void
mh_audio_close(struct mh_audio *audio)
{
	if (audio == NULL)
		return;

	/* Detach callbacks before releasing their owned PCM. */
	mh_audio_device_close(audio->device);
	mh_race_audio_trace_state(audio, "audio-state close");
	retain_sound_history(audio);

	if (audio->music_only && audio->music_trace)
		fprintf(stderr,
			"Music continuation close: track %u, frame %.3f, callbacks %lu, failed %d\n",
			audio->music.track, audio->music.mixer.voices[0].cursor,
			audio->music_callbacks, audio->music_output_failed);

	if (audio->music_output_failed)
		fprintf(stderr,
			"Results music stopped: callback could not render or submit PCM\n");

	if (audio->output_failed)
		fprintf(stderr,
			"Audio output stopped: callback could not render or submit PCM\n");

	mh_race_profile_destroy(audio->profile);

	mh_race_sound_assets_free(&audio->assets);
	mh_music_stop(&audio->music);
	mh_pause_sound_assets_free(&audio->menu);
	mh_ambient_assets_free(&audio->ambient);
	mh_surface_assets_free(&audio->surface);

	free(audio);
}

struct mh_audio *
mh_audio_open_results_from(const struct mh_audio_provider *provider,
	const struct mh_audio_decoder_provider *decoder, const char *asset_root,
	const char *directory, unsigned int volume,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, unsigned int trace)
{
	struct mh_audio *audio;
	struct mh_cd_request request;
	struct mh_error error;
	enum mh_error_code status;

	if (provider == NULL || volume == 0 || clock == NULL ||
		resources == NULL)
		return NULL;

	audio = (struct mh_audio *)calloc(1, sizeof(*audio));

	if (audio == NULL) {
		fprintf(stderr, "Season music disabled: out of memory\n");
		return NULL;
	}

	audio->clock = clock;
	audio->resources = resources;
	audio->decoder = decoder;
	audio->music_trace = (trace & MH_AUDIO_TRACE_MUSIC) != 0;
	audio->phase_trace = (trace & MH_AUDIO_TRACE_PHASES) != 0;
	status = mh_music_init_from(audio->decoder, audio->resources,
		asset_root, directory, volume, 0, &audio->music, &error);

	if (status == MH_ERROR_NONE)
		status = mh_cd_select(MH_CD_RESULTS, volume, MH_GAME_LEAGUE, 0,
			0, &request, &error);

	if (status == MH_ERROR_NONE && request.play)
		status = mh_music_play_clocked(
			&audio->music, request.track, audio->clock, &error);

	audio->mode = MH_GAME_LEAGUE;
	audio->results = 1;

	if (status == MH_ERROR_NONE)
		status = mh_race_audio_open_device(provider, audio, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_audio_continue_results(audio, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Season music disabled: %s\n", error.message);
		mh_audio_close(audio);
		return NULL;
	}

	fprintf(stderr, "Resumed season music: CD track %u\n",
		audio->music.track);

	return audio;
}

static enum mh_error_code
init_mixers(struct mh_audio *audio, const struct mh_track *track,
	const struct mh_audio_options *options, struct mh_error *error_out)
{
	unsigned int volume;
	double effects_gain;

	/* Saved byte -> integer device percent, just like menu edits. */
	volume = (unsigned int)(options->effects_volume *
			MH_RACE_AUDIO_VOLUME_MAX +
		.5);
	effects_gain = mh_race_audio_effects_gain(volume);
	mh_race_sound_init(
		MH_AUDIO_OUTPUT_RATE, effects_gain, &audio->sound, NULL);
	audio->course_track = (unsigned int)(track->cd_track & CD_TRACK_MASK);
	audio->mode = options->mode;
	audio->music_repeat = !options->no_music_loop;
	mh_mixer_init(MH_AUDIO_OUTPUT_RATE, effects_gain, &audio->ambient_mixer,
		NULL);
	mh_surface_sound_init(MH_AUDIO_OUTPUT_RATE, effects_gain,
		&audio->surface_sound, NULL);
	audio->surface_sound.crash = previous_crash;
	mh_pause_sound_init(
		MH_AUDIO_OUTPUT_RATE, effects_gain, &audio->menu_mixer, NULL);
	mh_sound_pool_init_channels(MH_RACE_AUDIO_BANKS * MH_MIXER_VOICES,
		mh_race_profile_capacity(options->spatial), &audio->pool, NULL);
	mh_mixer_bind(&audio->sound.mixer, &audio->pool,
		MH_RACE_AUDIO_RACE_BANK * MH_MIXER_VOICES, NULL);
	mh_mixer_bind(&audio->surface_sound.mixer, &audio->pool,
		MH_RACE_AUDIO_SURFACE_BANK * MH_MIXER_VOICES, NULL);
	mh_mixer_bind(&audio->ambient_mixer, &audio->pool,
		MH_RACE_AUDIO_AMBIENT_BANK * MH_MIXER_VOICES, NULL);
	mh_mixer_bind(&audio->menu_mixer, &audio->pool,
		MH_RACE_AUDIO_MENU_BANK * MH_MIXER_VOICES, NULL);

	if (options->spatial)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return mh_surface_history_restore(
		&audio->surface_sound, &previous_material, error_out);
}

static void
load_effect_assets(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_animation_source *animation_source,
	struct mh_audio *audio, const char *asset_root,
	const struct mh_track *track, const struct mh_audio_options *options)
{
	struct mh_error error;
	enum mh_error_code status;

	/* Load muted banks too: the in-race sound options can unmute them. */
	status = mh_race_sound_assets_load_from(horn_source, audio->decoder,
		audio->resources, asset_root, options->horns, &audio->assets,
		&error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Effects disabled: %s\n", error.message);
	else
		audio->race_bank_loaded = 1;

	if (track->materials_file[0] != 0) {
		status = mh_surface_assets_load_from(material_source,
			env_sound_source, audio->decoder, audio->resources,
			asset_root, track->materials_file,
			track->env_sound_file, &audio->surface, &error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "Surface sounds disabled: %s\n",
				error.message);
	}

	status = mh_pause_sound_assets_load_from(audio->decoder,
		audio->resources, asset_root, &audio->menu, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Menu sounds disabled: %s\n", error.message);

	if (track->env_sound_file[0] != 0) {
		status = mh_ambient_assets_load_from(env_sound_source,
			animation_source, audio->decoder, audio->resources,
			asset_root, track->env_sound_file,
			options->no_scenery ? NULL : track->scene_file,
			&audio->ambient, &error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "Ambience disabled: %s\n",
				error.message);
	}
}

static void
load_race_music(struct mh_audio *audio, const char *asset_root,
	const struct mh_track *track, const struct mh_audio_options *options)
{
	struct mh_cd_request request;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int volume;

	volume = (unsigned int)(options->music_volume *
			MH_RACE_AUDIO_VOLUME_MAX +
		.5);
	status = mh_music_init_from(audio->decoder, audio->resources,
		asset_root, options->music_root, volume, audio->music_repeat,
		&audio->music, &error);

	if (status == MH_ERROR_NONE)
		status = mh_cd_select(MH_CD_RACE, volume, audio->mode,
			track->cd_track, 0, &request, &error);

	if (status == MH_ERROR_NONE && request.play)
		status = mh_music_play_clocked(
			&audio->music, request.track, audio->clock, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Music disabled: %s\n", error.message);
	else if (request.play)
		fprintf(stderr, "Race music: CD track %u, repeat %d\n",
			request.track, audio->music_repeat);
}

struct mh_audio *
mh_audio_open_from(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_animation_source *animation_source,
	const struct mh_audio_provider *provider,
	const struct mh_audio_decoder_provider *decoder, const char *asset_root,
	const struct mh_track *track, const struct mh_world *world,
	const struct mh_audio_options *options,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, unsigned int trace)
{
	struct mh_audio *audio;
	struct mh_error error;
	enum mh_error_code status;

	if (provider == NULL || asset_root == NULL || track == NULL ||
		options == NULL || clock == NULL || resources == NULL ||
		!(options->effects_volume >= 0 &&
			options->effects_volume <= 1) ||
		!(options->music_volume >= 0 && options->music_volume <= 1) ||
		(options->no_scenery != 0 && options->no_scenery != 1) ||
		(options->no_music_loop != 0 && options->no_music_loop != 1) ||
		(options->manual_service != 0 &&
			options->manual_service != 1) ||
		(options->spatial < MH_RACE_AUDIO_SOFTWARE ||
			options->spatial > MH_RACE_AUDIO_A3D20) ||
		options->mode > MH_GAME_DEMO)
		return NULL;

	audio = (struct mh_audio *)calloc(1, sizeof(*audio));

	if (audio == NULL) {
		fprintf(stderr, "Audio disabled: out of memory\n");
		return NULL;
	}

	audio->clock = clock;
	audio->audio_profile = options->spatial;
	audio->resources = resources;
	audio->decoder = decoder;
	audio->music_trace = (trace & MH_AUDIO_TRACE_MUSIC) != 0;
	audio->phase_trace = (trace & MH_AUDIO_TRACE_PHASES) != 0;
	status = init_mixers(audio, track, options, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Surface history: %s\n", error.message);
		mh_audio_close(audio);
		return NULL;
	}

	load_effect_assets(horn_source, material_source, env_sound_source,
		animation_source, audio, asset_root, track, options);

	load_race_music(audio, asset_root, track, options);

	if (!audio->race_bank_loaded && audio->music.asset.file.data == NULL &&
		audio->ambient.bank.count == 0 &&
		audio->surface.bank.count == 0 &&
		audio->menu.files[0].data == NULL) {
		free(audio);
		return NULL;
	}

	audio->manual_service = options->manual_service;
	status = mh_race_profile_create(audio, options->spatial,
		audio->sound.mixer.gain, world, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_audio_open_device(provider, audio, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Audio disabled: %s\n", error.message);
		mh_audio_close(audio);
		return NULL;
	}

	audio->owns_sound_history = 1;
	mh_race_audio_trace_state(audio, "audio-state open");

	status = mh_audio_device_start(audio->device, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Audio disabled: %s\n", error.message);
		mh_audio_close(audio);
		return NULL;
	}

	audio->device_started = 1;

	return audio;
}

void
mh_race_audio_trace_state(const struct mh_audio *audio, const char *event)
{
	unsigned int i, active, pending, physical;
	struct mh_surface_history history;

	if (!audio->phase_trace)
		return;

	fprintf(stderr, "%s", event);

	for (i = 0; i < MH_RACE_LEGACY_CARS; i++)
		fprintf(stderr, " %u", audio->sound.gear[i]);

	for (i = 0; i < MH_RACE_LEGACY_CARS; i++)
		fprintf(stderr, " %d", audio->surface_sound.material[i]);

	for (i = 0; i < MH_RACE_LEGACY_CARS; i++)
		fprintf(stderr, " %d", audio->surface_sound.body_material[i]);

	for (i = 0; i < 3; i++)
		fprintf(stderr, " %d", audio->surface_sound.bound[i]);

	for (i = 0; i < 3; i++)
		fprintf(stderr, " %d", audio->surface_sound.body_bound[i]);

	fprintf(stderr, " %d %u %lu", audio->surface_sound.crash.bound,
		audio->surface_sound.crash.last_class,
		audio->surface_sound.random);

	for (i = 0; i < MH_COUNTDOWN_CUES; i++)
		fprintf(stderr, " %d", audio->sound.countdown[i]);

	fprintf(stderr, " %d %d %.9g", audio->sound.finish_latch,
		audio->sound.checkpoint_latch, audio->surface_sound.crash.gain);
	active = pending = physical = 0;

	for (i = 0; i < audio->pool.count; i++)
		active += audio->pool.records[i].active != 0;

	for (i = 0; i < MH_SOUND_PENDING; i++)
		pending += audio->pool.pending[i] != 0;

	/* The physical voice count is reported only for the software mixer. */
	if (audio->audio_profile == MH_RACE_AUDIO_SOFTWARE)
		physical = mh_race_profile_active_voices(audio->profile);

	fprintf(stderr, " %u %u %u %u %.17g %u %.17g %.17g %d %d\n", active,
		pending, physical, audio->music.track,
		audio->music.mixer.voices[0].cursor, audio->music.volume,
		audio->music.mixer.gain, audio->sound.mixer.gain,
		audio->surface_sound.crash.detached,
		audio->surface_sound.mixer.voices[MH_SURFACE_VOICE_CRASH]
			.active);

	/*
	 * The main trace covers eight cars. Report additional cars separately.
	 */
	fprintf(stderr, "audio-extra %s", event);

	for (i = MH_RACE_LEGACY_CARS; i < MH_RACE_MAX_CARS; i++)
		fprintf(stderr, " %u %d %d", audio->sound.gear[i],
			audio->surface_sound.material[i],
			audio->surface_sound.body_material[i]);

	fputc('\n', stderr);

	if (mh_surface_history_save(&audio->surface_sound, &history, NULL) ==
		MH_ERROR_NONE) {
		fprintf(stderr, "audio-material %s", event);

		for (i = 0; i < MH_SURFACE_HISTORY_VOICES; i++)
			fprintf(stderr, " %u %u", history.active[i],
				history.channel[i]);

		fputc('\n', stderr);
	}
}

static void
reset_locked(struct mh_audio *audio)
{
	if (audio == NULL || audio->music_only)
		return;

	mh_race_audio_trace_state(audio, "audio-restart before");

	/*
	 * Walk channel/pending pointers, retaining caches, shared bindings,
	 * RNG, cue latches and unreachable activity. This does not issue the
	 * separate physical-device stop.
	 */
	if (!mh_race_audio_clear_effects(audio))
		return;

	audio->menu_visible = 0;
	audio->owns_sound_history = 1;
	audio->results = 0;

	/*
	 * Restore CD volume only. The selected track, cursor and loop state
	 * survive; already queued PCM drains as for listener commands.
	 */
	mh_music_volume(&audio->music, audio->music.volume, NULL);
	mh_race_audio_trace_state(audio, "audio-restart after");
}

int
mh_audio_music_playing(const struct mh_audio *audio)
{
	int playing;

	/* Retain the selected track after natural completion. */
	if (audio == NULL ||
		mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	playing = audio->music.track != 0;
	mh_audio_device_unlock(audio->device, NULL);

	return playing;
}

static int
results_locked(struct mh_audio *audio, int aborted)
{
	struct mh_cd_request request;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i;

	if (audio == NULL || audio->results)
		return 1;

	status = mh_cd_select(MH_CD_RESULTS, audio->music.volume, audio->mode,
		0, audio->music.track, &request, &error);

	if (status != MH_ERROR_NONE)
		return 0;

	mh_race_audio_trace_state(audio, "audio-results before");
	retain_sound_history(audio);

	/*
	 * Tear down race effects before results, including Quit with the pause
	 * menu still visible. No queued menu request or looping physical voice
	 * may survive into the results stream.
	 */
	for (i = 0; i < MH_MIXER_VOICES; i++) {
		mh_mixer_stop(&audio->sound.mixer, i);
		mh_mixer_stop(&audio->surface_sound.mixer, i);
		mh_mixer_stop(&audio->ambient_mixer, i);
		mh_mixer_stop(&audio->menu_mixer, i);
	}

	if (mh_race_profile_finish(audio->profile, NULL) != MH_ERROR_NONE)
		return 0;

	/*
	 * Results continue music without effects. Inactive copied records can
	 * have pending aliases; discard those too so a later native update
	 * cannot restart their PCM.
	 */
	if (mh_sound_pool_init_channels(MH_RACE_AUDIO_BANKS * MH_MIXER_VOICES,
		mh_race_profile_capacity(audio->audio_profile),
		&audio->pool, NULL) != MH_ERROR_NONE)
		return 0;

	/*
	 * Results bypass the race's repeat caller; they do not rewrite CDLoop.
	 */
	audio->results = 1;
	mh_music_volume(&audio->music, audio->music.volume, NULL);

	/* European 3.1 bypasses the results song on abort. */
	if (request.play && !aborted) {
		status = mh_music_play_clocked(
			&audio->music, request.track, audio->clock, &error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "Results music disabled: %s\n",
				error.message);
		else
			fprintf(stderr, "Results music: CD track %u\n",
				request.track);
	}

	/*
	 * Effects and CD share native PCM. Clearing also drops at most the
	 * prefetched music block, while preserving its source cursor.
	 */
	if (mh_audio_device_clear(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	mh_race_audio_trace_state(audio, "audio-results after");

	return 1;
}

void
mh_audio_reset(struct mh_audio *audio)
{
	if (audio == NULL ||
		mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return;

	reset_locked(audio);
	mh_audio_device_unlock(audio->device, NULL);
}

int
mh_audio_results(struct mh_audio *audio, int aborted)
{
	int ok;

	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	ok = results_locked(audio, aborted);

	return mh_race_audio_unlock(audio, ok);
}

int
mh_audio_music_poll(struct mh_audio *audio, long double milliseconds)
{
	struct mh_error error;
	enum mh_error_code status;
	unsigned long started;
	double frame;

	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	started = audio->music.started_ms;
	frame = audio->music.mixer.voices[0].cursor;
	status = audio->music_only || audio->results
		? mh_error_set(&error, MH_ERROR_NONE, NULL)
		: mh_music_poll(&audio->music, milliseconds, &error);

	if (status == MH_ERROR_NONE && audio->music_trace &&
		audio->music.started_ms != started)
		fprintf(stderr,
			"Music repeat: track %u, start %lu -> %lu, duration %lu, frame %.3f -> %.3f\n",
			audio->music.track, started, audio->music.started_ms,
			audio->music.duration_ms, frame,
			audio->music.mixer.voices[0].cursor);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Music repeat: %s\n", error.message);

	return mh_race_audio_unlock(audio, status == MH_ERROR_NONE);
}
