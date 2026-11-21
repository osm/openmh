#ifndef MH_AUDIO_MUSIC_H
#define MH_AUDIO_MUSIC_H

#include "engine/audio/cd.h"
#include "engine/audio/mixer.h"
#include "engine/audio/music_asset.h"
#include "engine/core/error.h"

struct mh_resource_source;
struct mh_audio_decoder_provider;
struct mh_clock_source;

/*
 * Owned PCM and portable mixer; no device or worker thread. Keep this object
 * at a stable address while playing, since its voice refers to asset.wave.
 */
struct mh_music {
	/*
	 * Borrowed until the player is no longer used, including future plays.
	 */
	const struct mh_resource_source		*resources;
	const struct mh_audio_decoder_provider	*decoder;
	struct mh_music_asset			asset;
	struct mh_mixer				mixer;
	char					directory[1024];
	unsigned int				track, volume;
	unsigned long				started_ms, duration_ms;
	int					repeat;
};

/*
 * Initialize an empty object. NULL directory selects GAME_ROOT/Music.
 * Original integer-percent volume precedes the native .35 mix headroom.
 * Provider/source remain borrowed until the player is no longer used.
 */
enum mh_error_code	mh_music_init_from(
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *directory, unsigned int volume, int repeat,
	struct mh_music *music, struct mh_error *error_out);

/*
 * Stops/releases the preceding track, including on a missing/invalid encoded
 * track. Only one track is resident; no fallback to an unrelated song.
 */
enum mh_error_code	mh_music_play(struct mh_music *music,
	unsigned int track, long double milliseconds,
	struct mh_error *error_out);

/*
 * Live selections sample the clock after loading/starting PCM. The source is
 * borrowed for this call only.
 */
enum mh_error_code	mh_music_play_clocked(struct mh_music *music,
	unsigned int track, const struct mh_clock_source *clock,
	struct mh_error *error_out);

/*
 * Called once per race frame, including an open pause menu, never from the
 * PCM callback or menu/results loops. Reuses owned PCM on repeat.
 */
enum mh_error_code	mh_music_poll(struct mh_music *music,
	long double milliseconds, struct mh_error *error_out);
enum mh_error_code	mh_music_volume(struct mh_music *music,
	unsigned int volume, struct mh_error *error_out);

/*
 * Original finish changes device volume only: preserve preference, song
 * cursor, active state and repeat policy. Volume editing/results restore it.
 */
void	mh_music_mute(struct mh_music *music);
void	mh_music_stop(struct mh_music *music);

#endif
