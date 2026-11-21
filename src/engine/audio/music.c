#include <string.h>

#include "engine/assets/path_directories.h"
#include "engine/audio/music.h"
#include "engine/audio/music_asset.h"
#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "interfaces/audio/decoder.h"
#include "interfaces/clock/clock.h"

#define MUSIC_MIX_RATE 48000
/* Each volume percentage point adds this gain; 100% gives 0.35. */
#define MUSIC_GAIN_PER_PERCENT .0035

static double
volume_gain(unsigned int volume)
{
	/* Preserve integer-percent rounding before native mixer headroom. */
	return (volume * 100 / MH_CD_VOLUME_MAX) * MUSIC_GAIN_PER_PERCENT;
}

enum mh_error_code
mh_music_init_from(const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *directory, unsigned int volume, int repeat,
	struct mh_music *music, struct mh_error *error_out)
{
	struct mh_music next;
	enum mh_error_code status;

	if (source == NULL || music == NULL || volume > MH_CD_VOLUME_MAX ||
		(repeat != 0 && repeat != 1) ||
		(directory == NULL && root == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid music configuration");

	if ((directory != NULL &&
		strlen(directory) >= sizeof(next.directory)) ||
		(directory == NULL &&
			strlen(root) > sizeof(next.directory) -
					sizeof("/" MH_ASSET_MUSIC_DIR)))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"music directory path too long");

	if (mh_audio_decoder_extension(decoder) == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"music decoding is unavailable");

	memset(&next, 0, sizeof(next));

	if (directory != NULL)
		strcpy(next.directory, directory);
	else {
		strcpy(next.directory, root);
		strcat(next.directory, "/" MH_ASSET_MUSIC_DIR);
	}

	next.resources = source;
	next.decoder = decoder;
	next.repeat = repeat;
	next.volume = volume;
	status = mh_mixer_init(
		MUSIC_MIX_RATE, volume_gain(volume), &next.mixer, error_out);

	if (status == MH_ERROR_NONE)
		*music = next;

	return status;
}

void
mh_music_stop(struct mh_music *music)
{
	if (music == NULL)
		return;

	mh_mixer_stop(&music->mixer, 0);
	mh_music_asset_free(&music->asset);
	music->track = 0;
}

enum mh_error_code
mh_music_play(struct mh_music *music, unsigned int track,
	long double milliseconds, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned long started_ms;

	if (music == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing music player");

	status = mh_clock_word(milliseconds, &started_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_music_stop(music);
	status = mh_music_asset_load_from(music->decoder, music->resources,
		music->directory, track, &music->asset, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mixer_play(&music->mixer, 0, &music->asset.wave, 1,
			1, 1, 0, 0, error_out);

	if (status != MH_ERROR_NONE)
		mh_music_stop(music);
	else {
		music->track = track;
		music->started_ms = started_ms;

		/*
		 * MCI minutes/seconds contribute to duration; CD frames do not.
		 * Extracted stereo 44100-Hz PCM supplies the native duration.
		 */
		music->duration_ms = (unsigned long)(music->asset.wave.frames /
					music->asset.wave.rate) *
			1000;
	}

	return status;
}

enum mh_error_code
mh_music_play_clocked(struct mh_music *music, unsigned int track,
	const struct mh_clock_source *clock, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (clock == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing music clock");

	status = mh_music_play(music, track, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_clock_source_word(
			clock, &music->started_ms, error_out);

	if (status != MH_ERROR_NONE)
		mh_music_stop(music);

	return status;
}

enum mh_error_code
mh_music_poll(struct mh_music *music, long double milliseconds,
	struct mh_error *error_out)
{
	struct mh_cd_request request;
	unsigned long started_ms;
	enum mh_error_code status;

	if (music == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing music player");

	status = mh_cd_repeat_select(music->volume, (unsigned int)music->repeat,
		music->track, music->started_ms, music->duration_ms,
		milliseconds, &request, error_out);

	if (status != MH_ERROR_NONE || !request.play)
		return status;

	status = mh_clock_word(milliseconds, &started_ms, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mixer_play(&music->mixer, 0, &music->asset.wave, 1,
			1, 1, 0, 0, error_out);

	if (status == MH_ERROR_NONE)
		music->started_ms = started_ms;

	return status;
}

enum mh_error_code
mh_music_volume(
	struct mh_music *music, unsigned int volume, struct mh_error *error_out)
{
	if (music == NULL || volume > MH_CD_VOLUME_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid music volume");

	music->volume = volume;
	music->mixer.gain = volume_gain(volume);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_music_mute(struct mh_music *music)
{
	if (music != NULL)
		music->mixer.gain = 0;
}
