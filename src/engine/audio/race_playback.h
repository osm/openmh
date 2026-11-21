#ifndef MH_ENGINE_AUDIO_RACE_PLAYBACK_H
#define MH_ENGINE_AUDIO_RACE_PLAYBACK_H

#include "engine/audio/race_profile.h"
#include "engine/game/race_limits.h"

struct mh_horn_source;

struct mh_env_sound_source;
struct mh_material_source;
struct mh_animation_source;
struct mh_field;
struct mh_track;
struct mh_world;
struct mh_pause_output;
struct mh_clock_source;
struct mh_resource_source;

struct mh_audio_options {
	/*
	 * Select an original audio profile; zero lets race setup use saved
	 * preferences.
	 */
	int		spatial;
	double		effects_volume, music_volume; /* 0..1 */

	/* NULL: GAME_ROOT/Music, the extraction layout. */
	const char	*music_root;

	/* NULL entries select Honker1. */
	const char	*horns[MH_RACE_MAX_CARS];

	/* Omit object-bound ambience; retain fixed ESD emitters. */
	int		no_scenery;

	/* Saved CDLoop=Off; zero preserves default On. */
	int		no_music_loop;
	unsigned int	mode; /* Original session mode, 0..6. */

	/*
	 * Offline reference harness supplies callbacks; default zero uses the
	 * selected provider scheduler.
	 */
	int		manual_service;
};

struct mh_audio;
struct mh_audio_provider;
struct mh_audio_decoder_provider;

enum mh_audio_trace {
	MH_AUDIO_TRACE_MUSIC = 1,
	MH_AUDIO_TRACE_PHASES = 2
};

struct mh_audio_music_state {
	unsigned int	track;

	/* Saved preference, independent of finish mute. */
	unsigned int	volume;
	double		gain;

	/* Source frames submitted, including device buffering. */
	double		frame;
	unsigned long	callbacks, effects_callbacks;

	/* Original repeat-clock words. */
	unsigned long	started_ms, duration_ms;

	/* Ring writes, split writes, 22050 Hz frames consumed. */
	unsigned long	effects_fills, effects_wraps, effects_frames;
	unsigned int	effects_active, effects_pending, effects_channels;
	int		active, background, failed;
};

/*
 * Horn, material, environment-sound and animation sources are borrowed only
 * during open. Selected output/scheduler, decoder, resources and clock are
 * borrowed until close. The parsed world is borrowed until music-only
 * continuation; NULL world is an empty scene for audio-only diagnostics.
 *
 * NULL output provider means unavailable optional audio. Diagnostics are
 * explicit flags; these constructors do not read the native environment. NULL
 * on failure; the owner logs errors and may continue without audio. No worker
 * touches simulation state.
 *
 * The single active race inherits process crash class/gain/detached activity;
 * results/close capture it before releasing effects. Music-only objects do not
 * overwrite that history.
 */
struct mh_audio	*mh_audio_open_from(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_animation_source *animation_source,
	const struct mh_audio_provider *provider,
	const struct mh_audio_decoder_provider *decoder, const char *root,
	const struct mh_track *track, const struct mh_world *world,
	const struct mh_audio_options *options,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, unsigned int trace);
struct mh_audio	*mh_audio_open_results_from(
	const struct mh_audio_provider *provider,
	const struct mh_audio_decoder_provider *decoder, const char *root,
	const char *directory, unsigned int volume,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, unsigned int trace);

void	mh_audio_close(struct mh_audio *audio);

/*
 * In-session restart: run the original channel/pending stop walk, retaining
 * caches, RNG, HUD latches and unreachable/old copied sound state. Restore
 * saved CD volume without rewinding or selecting a song.
 * The field reset owns local listener reselection.
 */
void	mh_audio_reset(struct mh_audio *audio);

/* Original current-track latch, retained after natural song completion. */
int	mh_audio_music_playing(const struct mh_audio *audio);

/*
 * Poll the CD repeat timer after keyboard polling, before race/menu input
 * consumers. An open pause menu does not suppress it. Native focus/ manual
 * suspension is handled by the caller; finalized results suppress it.
 */
int	mh_audio_music_poll(struct mh_audio *audio, long double milliseconds);

/*
 * Finalized local results, not the temporary standings overlay. Stop all
 * race/menu effects and discard queued mixed PCM when entering results.
 * Aborted exits retain the current music.
 */
int	mh_audio_results(struct mh_audio *audio, int aborted);

/*
 * Keep the same results stream/queued PCM/cursor running during result
 * persistence and season loading/presentation. After this succeeds, update
 * only handles focus pause/resume; other race mutators are disabled. Its
 * callback touches owned music PCM, never game state.
 */
int	mh_audio_continue_results(struct mh_audio *audio);

/* A synchronized snapshot; NULL audio reports silent/zero state. */
int	mh_audio_music_inspect(struct mh_audio *audio,
	struct mh_audio_music_state *output);

/*
 * Deterministic device-fixture clock. Only valid for manual_service objects;
 * normal race/menu updates never call this or advance the periodic service.
 */
int	mh_audio_service(struct mh_audio *audio);

/*
 * Consume ordered controller sound/volume events. Menu visibility suppresses
 * race sound producers while effects callbacks and CD playback continue.
 */
int	mh_audio_menu(struct mh_audio *audio,
	const struct mh_pause_output *output, int visible);

/*
 * Listener, vehicles and ambience before the current frame's physics and route
 * publication. Call after view/menu commands but before publishing current
 * local horn input.
 */
int	mh_audio_frame_begin(struct mh_audio *audio,
	const struct mh_field *field, double pedal, int paused, int results);

/*
 * Publish current camera/car/emitter positions after simulation/camera update
 * and before rendering. No new sound requests, gain refresh or PCM service.
 */
int	mh_audio_positions(struct mh_audio *audio,
	const struct mh_field *field);

/*
 * HUD cues use new progress; held info suppresses countdown processing.
 * Producers update owned audio state under its stream lock. Independent
 * 40 ms callbacks admit/refresh physical effects. The selected output either
 * services the software ring or renders spatial PCM on device demand.
 */
int	mh_audio_frame_end(struct mh_audio *audio, struct mh_field *field,
	int paused, int results, unsigned int gap_mode, int info, int message);

/* Combined same-snapshot convenience for scripted previews. */
int	mh_audio_update(struct mh_audio *audio, struct mh_field *field,
	double pedal, int paused, int results, unsigned int gap_mode);

#endif
