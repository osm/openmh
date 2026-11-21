#ifndef MH_CLIENT_DIAGNOSTICS_MEDIA_AUDIO_H
#define MH_CLIENT_DIAGNOSTICS_MEDIA_AUDIO_H

#include "engine/audio/menu_playback.h"
#include "engine/audio/race_playback.h"

/*
 * NULL on failure; playback errors are printed by the owner.
 * Caller may continue without audio. No worker touches simulation state.
 * The single active race inherits process crash class/gain/detached activity;
 * results/close capture it before releasing effects. Music-only objects do
 * not own or overwrite that history.
 */
struct mh_audio	*mh_audio_open(const char *root, const struct mh_track *track,
	const struct mh_audio_options *options, unsigned int trace);

/*
 * Native completed-save recovery: a music-only results stream, starting at
 * track 2 rather than inventing a persisted CD position. NULL when muted or
 * unavailable; errors are logged and presentation can continue silently.
 */
struct mh_audio	*mh_audio_open_results(const char *root, const char *directory,
	unsigned int volume, unsigned int trace);

/*
 * Explicit source is required and borrowed until close. Music selection and
 * repeat sample it on the calling thread; PCM/worker callbacks never do.
 * mh_audio_open and mh_audio_open_results select the system clock.
 */
struct mh_audio	*mh_audio_open_with_clock(const char *root,
	const struct mh_track *track, const struct mh_audio_options *options,
	const struct mh_clock_source *clock, unsigned int trace);

struct mh_audio	*mh_audio_open_results_with_clock(const char *root,
	const char *directory, unsigned int volume,
	const struct mh_clock_source *clock, unsigned int trace);

/*
 * _with_sources borrows prepared resources and clock until close. Resource
 * callbacks run only on the calling thread, including later music selection;
 * workers consume owned PCM. Other constructors select filesystem resources.
 */
struct mh_audio	*mh_audio_open_with_sources(const char *root,
	const struct mh_track *track, const struct mh_audio_options *options,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, unsigned int trace);

struct mh_audio	*mh_audio_open_results_with_sources(const char *root,
	const char *directory, unsigned int volume,
	const struct mh_resource_source *resources,
	const struct mh_clock_source *clock, unsigned int trace);

/*
 * NULL on failure with an error on stderr. The supplied 0..1 volume becomes
 * original integer percent, applied locally to the device PCM.
 */
struct mh_menu_audio	*mh_menu_audio_open(const char *root, double volume);

/*
 * Source is required and borrowed until close; only calling-thread music
 * selection samples it. The compatibility constructor selects system time.
 */
struct mh_menu_audio	*mh_menu_audio_open_with_clock(const char *root,
	double volume, const struct mh_clock_source *clock);

/*
 * _with_sources borrows resources/clock until close. Later horn/music loads
 * use that source on the calling thread; workers consume owned PCM.
 * Other constructors select filesystem resources.
 */
struct mh_menu_audio	*mh_menu_audio_open_with_sources(const char *root,
	double volume, const struct mh_resource_source *resources,
	const struct mh_clock_source *clock);

#endif
