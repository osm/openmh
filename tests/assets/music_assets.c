#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "client/diagnostics/asset_audio.h"
#include "client/diagnostics/media_audio.h"
#include "client/diagnostics/options.h"
#include "engine/audio/menu_playback.h"
#include "engine/audio/music.h"
#include "engine/audio/race_playback.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/field.h"
#include "engine/ui/pause.h"
#include "interfaces/catalog/track.h"
#include "interfaces/clock/backend.h"

static struct mh_debug_options debug;

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Music line %d: %s\n", __LINE__, #x);  \
			return 1;                                              \
		}                                                              \
	} while (0)

struct track_clock {
	const struct mh_music	*music;
	unsigned int		calls;
};

static struct mh_clock_counter
track_counter(void *context)
{
	struct track_clock *clock;
	struct mh_clock_counter value;

	clock = context;
	clock->calls++;
	value.low = 4003;
	value.high = 0;

	if (clock->music->asset.file.data == NULL ||
		!clock->music->mixer.voices[0].active)
		value.high = MH_U32_SIGN_BIT;

	return value;
}

static struct mh_clock_counter
track_frequency(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = 4000;
	value.high = 0;

	return value;
}

static unsigned long
track_ticks(void *context)
{
	(void)context;

	return 0;
}

static int
wait_menu(struct mh_menu_audio *audio, double frame)
{
	struct mh_menu_audio_state state;
	Uint64 limit;

	limit = SDL_GetTicks() + 2000;

	do {
		if (!mh_menu_audio_inspect(audio, &state))
			return 0;

		if (state.music_frame > frame && state.effects_callbacks != 0)
			return 1;

		SDL_Delay(2);
	} while (SDL_GetTicks() < limit);

	return 0;
}

static int
wait_race(struct mh_audio *audio, double frame)
{
	struct mh_audio_music_state state;
	Uint64 limit;

	limit = SDL_GetTicks() + 2000;

	do {
		if (!mh_audio_music_inspect(audio, &state))
			return 0;

		if (state.frame > frame && state.effects_callbacks != 0 &&
			state.effects_fills > 1 && state.effects_wraps > 0 &&
			state.effects_frames != 0)
			return 1;

		SDL_Delay(2);
	} while (SDL_GetTicks() < limit);

	return 0;
}

static int
finish_audio_frame(struct mh_audio *audio, struct mh_field *field, int results)
{
	return mh_audio_frame_end(audio, field, 0, results, 0, 0, 0) &&
		mh_audio_service(audio);
}

static int
wait_channels(struct mh_audio *audio, unsigned int count,
	unsigned long after_callback)
{
	struct mh_audio_music_state state;
	Uint64 limit;

	limit = SDL_GetTicks() + 2000;

	do {
		if (!mh_audio_music_inspect(audio, &state) || state.failed)
			return 0;

		if (state.effects_channels == count &&
			state.effects_active == count &&
			state.effects_pending == 0 &&
			state.effects_callbacks > after_callback)
			return 1;

		SDL_Delay(2);
	} while (SDL_GetTicks() < limit);

	return 0;
}

static int
independent_race(const char *root, const char *music_root)
{
	struct mh_audio_options options;
	struct mh_track track;
	struct mh_audio_music_state before, after;
	struct mh_pause_output events;
	struct mh_audio *audio;
	unsigned int i;

	memset(&options, 0, sizeof(options));
	memset(&track, 0, sizeof(track));
	options.effects_volume = options.music_volume = 1;
	options.music_root = music_root;
	track.cd_track = 4;
	memset(&events, 0, sizeof(events));
	events.count = 1;

	for (i = 0; i < 2; i++) {
		audio = mh_audio_open(
			root, &track, &options, mh_debug_audio_trace(&debug));
		CHECK(audio != NULL);
		events.events[0].kind = MH_PAUSE_PLAY;
		events.events[0].value = 0;
		CHECK(mh_audio_menu(audio, &events, 1));
		CHECK(wait_channels(audio, 1, 0) && wait_race(audio, 0));

		/*
		 * Focus pause freezes PCM while independent timer wakes
		 * continue. An explicit producer stop still retires the
		 * physical loop now.
		 */
		CHECK(mh_audio_frame_begin(audio, NULL, 0, 1, 0));
		CHECK(mh_audio_music_inspect(audio, &before));
		SDL_Delay(160);
		CHECK(mh_audio_music_inspect(audio, &after) &&
			after.frame == before.frame &&
			after.effects_channels == 1 &&
			after.effects_callbacks > before.effects_callbacks &&
			after.effects_frames == before.effects_frames &&
			after.effects_fills == before.effects_fills);
		events.events[0].kind = MH_PAUSE_STOP;
		CHECK(mh_audio_menu(audio, &events, 1) &&
			mh_audio_music_inspect(audio, &after) &&
			after.effects_channels == 0 &&
			after.effects_pending == 0 &&
			after.effects_active == 0);
		CHECK(mh_audio_frame_begin(audio, NULL, 0, 0, 0));
		events.events[0].kind = MH_PAUSE_PLAY;
		CHECK(mh_audio_menu(audio, &events, 1) &&
			wait_channels(audio, 1, 0) &&
			wait_race(audio, before.frame));
		CHECK(mh_audio_music_inspect(audio, &after) &&
			after.effects_frames > before.effects_frames);

		if (i == 0) {
			CHECK(mh_audio_results(audio, 0) &&
				mh_audio_music_inspect(audio, &after) &&
				after.effects_channels == 0 &&
				after.effects_pending == 0 &&
				after.effects_active == 0);
		}

		/*
		 * Second close joins an active worker before freeing a playing
		 * loop.
		 */
		mh_audio_close(audio);
		CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	}

	return 0;
}

static int
repeat_race(const char *root, const char *music_root)
{
	struct mh_audio_options options;
	struct mh_track track;
	struct mh_audio_music_state before, after;
	struct mh_pause_output events;
	struct mh_audio *audio;
	unsigned int mode;
	long double due;

	memset(&options, 0, sizeof(options));
	memset(&track, 0, sizeof(track));
	memset(&events, 0, sizeof(events));
	events.count = 1;
	options.music_volume = 1;
	options.music_root = music_root;
	track.cd_track = 5;

	for (mode = 0; mode <= 2; mode += 2) {
		options.mode = mode;
		audio = mh_audio_open(
			root, &track, &options, mh_debug_audio_trace(&debug));
		CHECK(audio != NULL);

		/*
		 * Freeze the consumer to inspect exact cursor changes. The
		 * explicit clock poll exercises the same synchronized adapter
		 * as a live frame.
		 */
		CHECK(mh_audio_frame_begin(audio, NULL, 0, 1, 1));
		CHECK(mh_audio_music_inspect(audio, &before) &&
			before.duration_ms == 1000);
		due = (before.started_ms + before.duration_ms) & MH_U32_MAX;
		CHECK(mh_audio_music_poll(audio, due) &&
			mh_audio_music_inspect(audio, &after) &&
			after.started_ms == before.started_ms &&
			after.frame == before.frame);
		CHECK(mh_audio_music_poll(audio, due + .25L) &&
			mh_audio_music_inspect(audio, &after) &&
			after.started_ms == (unsigned long)due &&
			after.frame == 0 && after.active);
		events.events[0].kind = MH_PAUSE_CD_VOLUME;
		events.events[0].value = 0;
		CHECK(mh_audio_menu(audio, &events, 1) &&
			mh_audio_music_inspect(audio, &before));
		CHECK(mh_audio_music_poll(audio, due + 2000) &&
			mh_audio_music_inspect(audio, &after) &&
			after.started_ms == before.started_ms &&
			after.frame == before.frame);
		events.events[0].value = 254;
		CHECK(mh_audio_menu(audio, &events, 1));
		CHECK(mh_audio_music_poll(audio, due + 2000) &&
			mh_audio_music_inspect(audio, &after) &&
			after.started_ms ==
				(((unsigned long)due + 2000) & MH_U32_MAX) &&
			after.frame == 0);
		mh_audio_reset(audio);
		CHECK(mh_audio_music_inspect(audio, &before) &&
			before.started_ms == after.started_ms);
		CHECK(mh_audio_results(audio, 0) &&
			mh_audio_music_inspect(audio, &before));
		CHECK(mh_audio_music_poll(audio, 1000000000000.0L) &&
			mh_audio_music_inspect(audio, &after) &&
			after.started_ms == before.started_ms &&
			after.frame == before.frame);

		/*
		 * Native standalone Home can return from its board to a race.
		 * Keep CDLoop as a preference; suppress it by lifecycle, not by
		 * clearing it.
		 */
		mh_audio_reset(audio);
		CHECK(mh_audio_music_inspect(audio, &before) &&
			before.started_ms == after.started_ms);
		due = (before.started_ms + before.duration_ms) & MH_U32_MAX;
		CHECK(mh_audio_music_poll(audio, due + 1) &&
			mh_audio_music_inspect(audio, &after) &&
			after.started_ms ==
				(((unsigned long)due + 1) & MH_U32_MAX) &&
			after.frame == 0);
		mh_audio_close(audio);
	}

	CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);

	return 0;
}

static int
independent_positions(const char *root)
{
	struct mh_audio_options options;
	struct mh_track track;
	struct mh_audio_music_state before, after;
	static struct mh_field field;
	struct mh_audio *audio;
	static const double rpm[] = {-1000000, -1923, 0, 1000000};
	unsigned int i;

	memset(&options, 0, sizeof(options));
	memset(&track, 0, sizeof(track));
	memset(&field, 0, sizeof(field));
	options.effects_volume = 1;
	field.count = field.active_mask = 1;
	field.network = 1;
	field.cars[0].vehicle.engine.rpm = 3000;
	field.cars[0].camera.rotation[0] = field.cars[0].camera.rotation[4] =
		field.cars[0].camera.rotation[8] = 1;
	field.cars[0].vehicle.motion.rotation[0] =
		field.cars[0].vehicle.motion.rotation[4] =
			field.cars[0].vehicle.motion.rotation[8] = 1;
	audio = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(audio != NULL);
	CHECK(mh_audio_frame_begin(audio, &field, 0, 0, 0) &&
		wait_channels(audio, 1, 0));
	field.cars[0].remote = 1;

	for (i = 0; i < sizeof(rpm) / sizeof(rpm[0]); i++) {
		CHECK(mh_audio_music_inspect(audio, &before));
		field.cars[0].vehicle.engine.rpm = rpm[i];
		CHECK(mh_audio_frame_begin(audio, &field, 0, 0, 0) &&
			wait_channels(audio, 1, before.effects_callbacks));
		CHECK(field.cars[0].vehicle.engine.rpm == rpm[i]);
	}

	CHECK(mh_audio_music_inspect(audio, &before));

	/*
	 * The real independent worker must see movement without another frame's
	 * producer pass. A stationary car also goes out of range as the camera
	 * moves.
	 */
	field.cars[0].camera.position[2] = 200;
	CHECK(mh_audio_positions(audio, &field) &&
		wait_channels(audio, 0, before.effects_callbacks));
	CHECK(mh_audio_music_inspect(audio, &before));
	field.cars[0].camera.position[2] = 0;
	CHECK(mh_audio_positions(audio, &field) &&
		wait_channels(audio, 0, before.effects_callbacks));
	CHECK(mh_audio_frame_begin(audio, &field, 0, 0, 0) &&
		wait_channels(audio, 1, 0));
	CHECK(mh_audio_music_inspect(audio, &before));
	field.cars[0].vehicle.motion.position[0] = 200;
	CHECK(mh_audio_positions(audio, &field) &&
		wait_channels(audio, 0, before.effects_callbacks));
	CHECK(mh_audio_music_inspect(audio, &after) && !after.failed);
	mh_audio_close(audio);
	CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);

	return 0;
}

static int
crash_lifetime(const char *root, const char *music_root)
{
	struct mh_audio_options options;
	struct mh_track track;
	static struct mh_field field;
	struct mh_audio *race, *retained, *music;

	memset(&options, 0, sizeof(options));
	memset(&track, 0, sizeof(track));
	memset(&field, 0, sizeof(field));
	options.effects_volume = 1;
	options.manual_service = 1;
	strcpy(track.materials_file, "Tracks/track1/Objects/redrock.SRF");
	strcpy(track.env_sound_file, "Tracks/track1/Data/ESounds.esd");
	field.count = 1;
	field.active_mask = field.listener_mask = 1;
	field.cars[0].target[2] = 1;
	field.cars[0].camera.rotation[0] = field.cars[0].camera.rotation[4] =
		field.cars[0].camera.rotation[8] = 1;
	field.cars[0].vehicle.motion.rotation[0] =
		field.cars[0].vehicle.motion.rotation[4] =
			field.cars[0].vehicle.motion.rotation[8] = 1;
	field.cars[0].impact_effects.contact = 1;
	field.cars[0].impact_effects.severity = 15;
	fprintf(stderr, "crash-lifetime 1\n");
	race = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(race != NULL);
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));

	/*
	 * Quit before the callback: capture a pending crash, then shut down
	 * PCM.
	 */
	CHECK(mh_audio_results(race, 0));
	mh_audio_close(race);
	fprintf(stderr, "crash-lifetime 2\n");
	music = mh_audio_open_results(
		root, music_root, 255, mh_debug_audio_trace(&debug));
	CHECK(music != NULL);
	mh_audio_close(music);
	race = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(race != NULL);
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	mh_audio_reset(race);
	field.listener_revision++;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	field.cars[0].impact_effects.sound_class = 2; /* Missing on Red Rock. */
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	mh_audio_close(race);
	fprintf(stderr, "crash-lifetime 3\n");
	retained = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(retained != NULL);
	field.cars[0].impact_effects.sound_class =
		1; /* Registered cone sound. */
	CHECK(mh_audio_frame_begin(retained, &field, 0, 0, 0));
	CHECK(mh_audio_results(retained, 0) &&
		mh_audio_continue_results(retained));
	fprintf(stderr, "crash-lifetime 4\n");
	race = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(race != NULL);
	field.cars[0].impact_effects.sound_class = 0;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	mh_audio_reset(race);
	mh_audio_close(race);

	/*
	 * A previous results stream must not overwrite the newer race history.
	 */
	mh_audio_close(retained);
	fprintf(stderr, "crash-lifetime 5\n");
	race = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(race != NULL);
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	CHECK(finish_audio_frame(race, &field, 0));
	CHECK(mh_audio_results(race, 0));
	mh_audio_close(race);
	fprintf(stderr, "crash-lifetime 6\n");
	race = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(race != NULL);
	field.cars[0].impact_effects.sound_class = 1;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	mh_audio_reset(race);
	mh_audio_close(race);
	fprintf(stderr, "crash-lifetime 7\n");
	race = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(race != NULL);
	field.cars[0].impact_effects.sound_class = 0;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	field.cars[0].impact_effects.sound_class = 1;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));

	/*
	 * Replacing the still-pending copy leaves an inactive queued record.
	 * Full results teardown must discard it before any later frame update.
	 */
	CHECK(mh_audio_results(race, 0));
	CHECK(finish_audio_frame(race, &field, 1));
	mh_audio_close(race);
	fprintf(stderr, "crash-lifetime 8\n");
	race = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(race != NULL);
	field.cars[0].impact_effects.sound_class = 0;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0) &&
		finish_audio_frame(race, &field, 0));
	field.cars[0].impact_effects.sound_class = 1;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0) &&
		finish_audio_frame(race, &field, 0));

	/*
	 * Restart stops only the newest crash channel; the older physical copy
	 * survives a subsequent listener command until full results teardown.
	 */
	mh_audio_reset(race);
	field.cars[0].impact_effects.contact = 0;
	field.listener_revision++;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	CHECK(mh_audio_results(race, 0));
	mh_audio_close(race);
	fprintf(stderr, "crash-lifetime 9\n");
	race = mh_audio_open(
		root, &track, &options, mh_debug_audio_trace(&debug));
	CHECK(race != NULL);
	field.cars[0].impact_effects.contact = 1;
	field.cars[0].impact_effects.sound_class = 0;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	field.cars[0].impact_effects.sound_class = 1;
	CHECK(mh_audio_frame_begin(race, &field, 0, 0, 0));
	mh_audio_reset(race);

	/*
	 * Original global clear ignores an inactive pending copy. Its first
	 * physical start can occur in the callback after the restart.
	 */
	CHECK(finish_audio_frame(race, &field, 0));
	CHECK(mh_audio_results(race, 0));
	mh_audio_close(race);
	fprintf(stderr, "crash-lifetime end\n");
	CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);

	return 0;
}

int
main(int argc, char *argv[])
{
	static const struct mh_clock_ops clock_ops = {
		track_counter, track_frequency, track_ticks, NULL, NULL};

	struct mh_clock_source clock;
	struct track_clock clock_state;
	struct mh_music music, saved;
	struct mh_menu_audio *menu_audio;
	struct mh_menu_audio_state levels, saved_levels;
	struct mh_audio *race;
	struct mh_audio_options options;
	struct mh_audio_music_state before, after;
	struct mh_pause_output events;
	struct mh_track track;
	static struct mh_field field;
	float samples[1024];
	unsigned int i, song;
	FILE *output;
	int success;

	if (mh_debug_parse_arguments(&debug, &argc, argv, NULL) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "Invalid diagnostic option or value.\n");
		return 1;
	}

	CHECK(argc == 5);

	/* Short synthetic track: 441 stereo frames, left .5, right -.25. */
	CHECK(mh_music_init(argv[1], argv[2], 255, 0, &music, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_music_play(&music, 2, 1000.25L, NULL) == MH_ERROR_NONE &&
		music.track == 2);
	CHECK(mh_mixer_render(&music.mixer, samples, 512, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 479; i++)
		CHECK(fabs(samples[i * 2] - .175) < .000001 &&
			fabs(samples[i * 2 + 1] + .0875) < .000001);

	for (i = 481; i < 512; i++)
		CHECK(samples[i * 2] == 0 && samples[i * 2 + 1] == 0);

	CHECK(!music.mixer.voices[0].active && music.track == 2);
	music.repeat = 1;
	CHECK(mh_music_play(&music, 2, 1000.25L, NULL) == MH_ERROR_NONE &&
		music.started_ms == 1000 && music.duration_ms == 0);
	CHECK(mh_mixer_render(&music.mixer, samples, 512, 0, NULL) ==
			MH_ERROR_NONE &&
		!music.mixer.voices[0].active);
	CHECK(mh_music_poll(&music, 1000, NULL) == MH_ERROR_NONE &&
		!music.mixer.voices[0].active);
	CHECK(mh_music_poll(&music, 1000.5L, NULL) == MH_ERROR_NONE &&
		music.mixer.voices[0].active &&
		music.mixer.voices[0].cursor == 0);
	CHECK(mh_mixer_render(&music.mixer, samples, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(samples[0] - .175) < .000001 &&
		fabs(samples[1] + .0875) < .000001);
	saved = music;
	mh_music_mute(&music);
	mh_music_mute(NULL);
	CHECK(music.volume == saved.volume && music.repeat == saved.repeat &&
		music.track == saved.track &&
		music.mixer.voices[0].cursor == saved.mixer.voices[0].cursor &&
		music.mixer.voices[0].loop_end ==
			saved.mixer.voices[0].loop_end);
	CHECK(mh_mixer_render(&music.mixer, samples, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		samples[0] == 0 && samples[1] == 0 &&
		music.mixer.voices[0].active &&
		music.mixer.voices[0].cursor ==
			saved.mixer.voices[0].cursor + 44100.0 / 48000.0);
	saved = music;
	CHECK(mh_music_volume(&music, 256, NULL) != MH_ERROR_NONE &&
		memcmp(&music, &saved, sizeof(music)) == 0);
	CHECK(mh_music_volume(&music, 128, NULL) == MH_ERROR_NONE &&
		fabs(music.mixer.gain - .175) < .000001);
	CHECK(mh_music_volume(&music, 0, NULL) == MH_ERROR_NONE &&
		music.mixer.voices[0].loop_end == 0);
	saved = music;
	CHECK(mh_music_poll(&music, 9000, NULL) == MH_ERROR_NONE &&
		music.started_ms == saved.started_ms &&
		music.mixer.voices[0].cursor == saved.mixer.voices[0].cursor);
	CHECK(mh_mixer_render(&music.mixer, samples, 512, 0, NULL) ==
			MH_ERROR_NONE &&
		!music.mixer.voices[0].active);

	for (i = 0; i < 1024; i++)
		CHECK(samples[i] == 0);

	CHECK(mh_music_volume(&music, 255, NULL) == MH_ERROR_NONE &&
		mh_music_poll(&music, 9000, NULL) == MH_ERROR_NONE &&
		music.started_ms == 9000 && music.mixer.voices[0].active);
	saved = music;
	CHECK(mh_music_play(&music, 2, -1, NULL) != MH_ERROR_NONE &&
		memcmp(&music, &saved, sizeof(music)) == 0);
	CHECK(mh_music_poll(&music, -1, NULL) != MH_ERROR_NONE &&
		memcmp(&music, &saved, sizeof(music)) == 0);
	CHECK(mh_music_play(&music, 3, 0, NULL) != MH_ERROR_NONE &&
		music.track == 0 && music.asset.file.data == NULL);
	CHECK(mh_music_play(&music, 4, 0, NULL) != MH_ERROR_NONE &&
		music.track == 0 && !music.mixer.voices[0].active);
	CHECK(mh_music_play(&music, 1, 0, NULL) != MH_ERROR_NONE &&
		mh_music_play(&music, 256, 0, NULL) != MH_ERROR_NONE);

	/*
	 * A 1.25-second WAV restarts at the first poll strictly past one
	 * second, cutting its fractional tail. Volume edits and finish mute
	 * retain time.
	 */
	clock_state.music = &music;
	clock_state.calls = 0;
	CHECK(mh_clock_source_init(&clock, &clock_ops, &clock_state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_music_play_clocked(&music, 3, &clock, NULL) != MH_ERROR_NONE &&
		clock_state.calls == 0);
	CHECK(mh_music_play_clocked(&music, 5, &clock, NULL) == MH_ERROR_NONE &&
		clock_state.calls == 1 && music.started_ms == 1000 &&
		music.duration_ms == 1000);
	CHECK(mh_mixer_render(&music.mixer, samples, 512, 0, NULL) ==
		MH_ERROR_NONE);
	saved = music;
	CHECK(mh_music_poll(&music, 2000, NULL) == MH_ERROR_NONE &&
		music.mixer.voices[0].cursor == saved.mixer.voices[0].cursor);
	mh_music_mute(&music);
	CHECK(mh_music_poll(&music, 2000.25L, NULL) == MH_ERROR_NONE &&
		music.started_ms == 2000 && music.mixer.voices[0].cursor == 0 &&
		music.mixer.gain == 0 && music.volume == 255);
	CHECK(music.asset.file.data == saved.asset.file.data);
	CHECK(mh_mixer_render(&music.mixer, samples, 512, 0, NULL) ==
		MH_ERROR_NONE);
	music.repeat = 0;
	saved = music;
	CHECK(mh_music_poll(&music, 9999, NULL) == MH_ERROR_NONE &&
		memcmp(&music, &saved, sizeof(music)) == 0);
	mh_music_stop(&music);
	mh_music_stop(&music);

	/* Extracted PCM; independent Python reference checks these samples. */
	CHECK(mh_music_init(argv[1], argv[3], 255, 0, &music, NULL) ==
		MH_ERROR_NONE);
	output = fopen(argv[4], "wb");
	CHECK(output != NULL);
	success = 1;

	for (song = 2; song <= 11; song++) {
		CHECK(mh_music_play(&music, song, 0, NULL) == MH_ERROR_NONE &&
			music.track == song);
		music.mixer.voices[0].cursor = music.asset.wave.frames / 2;
		CHECK(mh_mixer_render(&music.mixer, samples, 512, 0, NULL) ==
			MH_ERROR_NONE);

		if (fwrite(samples, sizeof(float), 1024, output) != 1024)
			success = 0;
	}

	if (fclose(output) != 0)
		success = 0;

	mh_music_stop(&music);
	CHECK(success);

	/* Missing music must leave the shared menu-effects device usable. */
	CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	menu_audio = mh_menu_audio_open(argv[1], .8);
	CHECK(menu_audio != NULL);
	mh_menu_audio_music(menu_audio, argv[1], argv[2], 255, 258);
	CHECK(mh_menu_audio_play(menu_audio, 1) &&
		mh_menu_audio_update(menu_audio));
	mh_menu_audio_music(menu_audio, argv[1], argv[2], 255, 4);
	CHECK(mh_menu_audio_play(menu_audio, 2) &&
		mh_menu_audio_update(menu_audio));
	CHECK(mh_menu_audio_music_volume(menu_audio, 0) &&
		mh_menu_audio_current_track(menu_audio) == 0);
	mh_menu_audio_music_play(menu_audio, 2);

	/*
	 * This ten-ms song can finish on the device thread before inspection.
	 * A completed cursor proves playback; a never-started voice stays at 0.
	 */
	CHECK(mh_menu_audio_inspect(menu_audio, &levels) && levels.track == 2 &&
		levels.music_volume == 0 && levels.music_gain == 0 &&
		(levels.music_active ||
			(levels.music_frame >= 441 &&
				levels.music_frame < 442)));
	CHECK(mh_menu_audio_music_volume(menu_audio, 128) &&
		mh_menu_audio_effects_volume(menu_audio, 128));
	CHECK(mh_menu_audio_inspect(menu_audio, &levels) &&
		levels.music_gain == 50 * .0035 &&
		levels.effects_gain == 50 / 800.0);
	saved_levels = levels;
	CHECK(!mh_menu_audio_music_volume(menu_audio, 256) &&
		!mh_menu_audio_effects_volume(menu_audio, 256));
	CHECK(mh_menu_audio_inspect(menu_audio, &levels) &&
		levels.effects_gain == saved_levels.effects_gain &&
		levels.music_gain == saved_levels.music_gain &&
		levels.music_volume == saved_levels.music_volume &&
		levels.track == saved_levels.track &&
		levels.music_frame >= saved_levels.music_frame);
	CHECK(mh_menu_audio_update(menu_audio) &&
		mh_menu_audio_current_track(menu_audio) == 2);
	mh_menu_audio_music_stop(menu_audio);
	CHECK(mh_menu_audio_inspect(menu_audio, &levels) && levels.track == 0 &&
		!levels.music_active && levels.music_volume == 128);
	CHECK(mh_menu_audio_music_volume(menu_audio, 255) &&
		mh_menu_audio_current_track(menu_audio) == 0);
	mh_menu_audio_music_play(
		menu_audio, 3); /* Malformed WAV stays silent. */
	CHECK(mh_menu_audio_current_track(menu_audio) == 0 &&
		mh_menu_audio_play(menu_audio, 1));
	CHECK(mh_menu_audio_inspect(NULL, &levels) && levels.track == 0 &&
		levels.effects_gain == 0);
	CHECK(!mh_menu_audio_inspect(menu_audio, NULL) &&
		mh_menu_audio_effects_volume(NULL, 0) &&
		mh_menu_audio_music_volume(NULL, 255) &&
		!mh_menu_audio_effects_volume(NULL, 256));
	mh_menu_audio_music_stop(NULL);
	mh_menu_audio_music_play(NULL, 2);

	/*
	 * A completed-save music-only device must not take the frontend's SDL
	 * audio reference with it. Its short song ends once without restarting.
	 */
	race = mh_audio_open_results(
		argv[1], argv[2], 255, mh_debug_audio_trace(&debug));
	CHECK(race != NULL);
	SDL_Delay(150);
	CHECK(mh_audio_music_inspect(race, &before) && before.background &&
		before.callbacks != 0 && before.track == 2 && !before.active &&
		!before.failed);
	SDL_Delay(50);
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.frame == before.frame &&
		after.callbacks > before.callbacks);
	mh_audio_close(race);
	CHECK(SDL_WasInit(SDL_INIT_AUDIO) != 0);
	CHECK(mh_menu_audio_play(menu_audio, 1) &&
		mh_menu_audio_update(menu_audio));

	/*
	 * Lobby inherits the actual song cursor, but no menu sample voices.
	 * The independent worker advances it without main-thread updates.
	 */
	mh_menu_audio_music(menu_audio, argv[1], argv[3], 255, 3);
	CHECK(wait_menu(menu_audio, 0));
	CHECK(mh_menu_audio_play(menu_audio, 1) &&
		mh_menu_audio_update(menu_audio));
	CHECK(mh_menu_audio_inspect(menu_audio, &saved_levels) &&
		saved_levels.music_active && saved_levels.effects_active);
	CHECK(mh_menu_audio_leave(menu_audio) && mh_menu_audio_leave(NULL));
	CHECK(mh_menu_audio_inspect(menu_audio, &levels) &&
		levels.track == saved_levels.track &&
		levels.music_frame >= saved_levels.music_frame &&
		levels.music_active && !levels.effects_active);
	CHECK(wait_menu(menu_audio, saved_levels.music_frame) &&
		mh_menu_audio_inspect(menu_audio, &levels) &&
		levels.music_frame > saved_levels.music_frame &&
		levels.music_active && !levels.effects_active);
	mh_menu_audio_close(menu_audio);
	CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	CHECK(mh_audio_open_results(argv[1], argv[2], 0,
		mh_debug_audio_trace(&debug)) == NULL &&
		SDL_WasInit(SDL_INIT_AUDIO) == 0);
	CHECK(mh_audio_open_results(argv[1], argv[2], 256,
		mh_debug_audio_trace(&debug)) == NULL &&
		SDL_WasInit(SDL_INIT_AUDIO) == 0);
	memset(&options, 0, sizeof(options));
	memset(&track, 0, sizeof(track));
	options.music_root = argv[3];
	options.music_volume = 1;
	track.cd_track = 4;
	race = mh_audio_open(
		argv[1], &track, &options, mh_debug_audio_trace(&debug));
	CHECK(race != NULL && mh_audio_music_playing(race));

	/*
	 * Watching an opponent finish mutes the current song even under MENU
	 * and held Info. Preference/cursor survive, and the shared latch
	 * permits an explicit volume command until an unfinished car is viewed.
	 */
	memset(&field, 0, sizeof(field));
	field.listener_mask = 3;
	field.count = 2;
	field.active_mask = 3;
	field.viewed_entry = 1;
	field.cars[1].race.finished = 1;
	memset(&events, 0, sizeof(events));
	CHECK(mh_audio_menu(race, &events, 1));
	CHECK(mh_audio_music_inspect(race, &before) && before.volume == 255 &&
		before.gain == 100 * .0035);
	CHECK(mh_audio_frame_end(race, &field, 0, 0, 0, 1, 1) &&
		mh_field_listener_positional(&field) &&
		field.listener_mask == 1);
	CHECK(mh_audio_music_inspect(race, &after) && after.volume == 255 &&
		after.gain == 0 && after.track == 4 && after.active &&
		after.frame >= before.frame);
	events.count = 1;
	events.events[0].kind = MH_PAUSE_CD_VOLUME;
	events.events[0].value = 128;
	CHECK(mh_audio_menu(race, &events, 1) &&
		mh_audio_frame_end(race, &field, 0, 0, 0, 1, 0));
	CHECK(mh_audio_music_inspect(race, &after) && after.volume == 128 &&
		after.gain == 50 * .0035);
	field.viewed_entry = 0;
	CHECK(mh_audio_frame_end(race, &field, 0, 0, 0, 1, 0));
	field.viewed_entry = 1;
	CHECK(mh_audio_frame_end(race, &field, 0, 0, 0, 1, 0));
	CHECK(mh_audio_music_inspect(race, &after) && after.volume == 128 &&
		after.gain == 0);
	before = after;
	mh_audio_reset(race);
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.track == before.track && after.frame >= before.frame &&
		after.active == before.active && after.volume == 128 &&
		after.gain == 50 * .0035);
	CHECK(!mh_audio_continue_results(race));
	CHECK(mh_audio_results(race, 0) && mh_audio_results(race, 0));
	CHECK(mh_audio_frame_end(race, &field, 0, 1, 0, 1, 0));
	CHECK(mh_audio_music_inspect(race, &after) && after.track == 2 &&
		after.gain == 50 * .0035);
	before = after;
	mh_audio_reset(race);
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.track == before.track && after.frame >= before.frame &&
		after.gain == 50 * .0035);
	CHECK(mh_audio_music_playing(race));
	CHECK(mh_audio_results(race, 0));
	memset(&events, 0, sizeof(events));
	CHECK(mh_audio_menu(race, &events, 1) &&
		mh_audio_update(race, NULL, 0, 0, 1, 0));
	CHECK(!mh_audio_service(race) && wait_race(race, 0));
	CHECK(mh_audio_music_inspect(race, &before) && before.track == 2 &&
		before.frame > 0 && before.active && !before.background &&
		before.callbacks == 0);
	CHECK(mh_audio_continue_results(race) &&
		mh_audio_continue_results(race));
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.frame >= before.frame && after.background);

	/*
	 * No main-thread update while cleanup/season loading would be blocked.
	 */
	SDL_Delay(300);
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.frame > before.frame + 4410 && after.active &&
		after.callbacks > 0 && !after.failed);
	before = after;
	mh_audio_reset(race);
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.frame >= before.frame);
	CHECK(!mh_audio_menu(race, &events, 0));
	CHECK(mh_audio_update(race, NULL, 0, 1, 1, 0));
	SDL_Delay(50);
	CHECK(mh_audio_music_inspect(race, &before));
	SDL_Delay(50);
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.frame == before.frame);
	CHECK(mh_audio_update(race, NULL, 0, 0, 1, 0));
	SDL_Delay(100);
	CHECK(mh_audio_music_inspect(race, &after) &&
		after.frame > before.frame && !after.failed);
	mh_audio_close(race);
	CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	CHECK(independent_race(argv[1], argv[3]) == 0);
	CHECK(repeat_race(argv[1], argv[2]) == 0);
	CHECK(independent_positions(argv[1]) == 0);
	CHECK(crash_lifetime(argv[1], argv[3]) == 0);

	/*
	 * Quit with a playing menu loop/confirmation and a pending cursor
	 * sound. Exercise every mode with CD enabled and muted: teardown must
	 * not depend on starting a different song or on closing the pause menu
	 * first.
	 */
	for (i = 0; i < 14; i++) {
		options.mode = i % 7;
		options.effects_volume = 1;
		options.music_volume = i / 7;
		options.manual_service = 1;
		race = mh_audio_open(argv[1], &track, &options,
			mh_debug_audio_trace(&debug));
		CHECK(race != NULL);
		memset(&events, 0, sizeof(events));
		events.count = 2;
		events.events[0].kind = events.events[1].kind = MH_PAUSE_PLAY;
		events.events[0].value = 0;
		events.events[1].value = 1;
		CHECK(mh_audio_menu(race, &events, 1) &&
			finish_audio_frame(race, NULL, 0));
		events.count = 1;
		events.events[0].value = 2;
		CHECK(mh_audio_menu(race, &events, 1));
		CHECK(mh_audio_music_inspect(race, &before));
		CHECK(mh_audio_results(race, 0) && mh_audio_results(race, 0));
		CHECK(mh_audio_music_inspect(race, &after));

		if (options.mode > 1 || options.music_volume == 0)
			CHECK(after.track == before.track &&
				after.frame == before.frame);
		else
			CHECK(after.track == 2 && after.frame == 0);
		CHECK(mh_audio_frame_end(race, NULL, 0, 1, 0, 0, 0));
		CHECK(mh_audio_frame_end(race, NULL, 0, 1, 0, 0, 0));
		mh_audio_close(race);
		CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	}

	CHECK(mh_audio_music_inspect(NULL, &after) && after.track == 0 &&
		!after.background);
	puts("Music PCM, loop/failure, results cursor continuation and shared SDL lifecycle passed");

	return 0;
}
