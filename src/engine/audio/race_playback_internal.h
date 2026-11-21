#ifndef MH_RACE_AUDIO_INTERNAL_H
#define MH_RACE_AUDIO_INTERNAL_H

#include "engine/audio/ambient_assets.h"
#include "engine/audio/music.h"
#include "engine/audio/pause_sound_assets.h"
#include "engine/audio/race_playback.h"
#include "engine/audio/race_profile_internal.h"
#include "engine/audio/race_sound_assets.h"
#include "engine/audio/surface_assets.h"
#include "engine/core/error.h"
#include "interfaces/audio/device.h"

#define MH_RACE_AUDIO_QUEUE_FRAMES 2048
#define MH_RACE_AUDIO_VOLUME_MAX 255
#define MH_RACE_AUDIO_ERROR_SIZE 256

enum mh_race_audio_bank {
	MH_RACE_AUDIO_RACE_BANK,
	MH_RACE_AUDIO_SURFACE_BANK,
	MH_RACE_AUDIO_AMBIENT_BANK,
	MH_RACE_AUDIO_MENU_BANK,
	MH_RACE_AUDIO_BANKS
};

/* Owned by the race; callbacks borrow it until device teardown. */
struct mh_audio {
	const struct mh_audio_decoder_provider	*decoder;
	const struct mh_resource_source		*resources;
	const struct mh_clock_source		*clock;
	struct mh_race_sound_assets		assets;
	struct mh_race_sound			sound;
	struct mh_music				music;
	unsigned int				course_track, mode;
	int					music_repeat, results;
	struct mh_ambient_assets		ambient;
	struct mh_mixer				ambient_mixer;
	struct mh_surface_assets		surface;
	struct mh_surface_sound			surface_sound;
	struct mh_sound_pool			pool;
	struct mh_race_profile			*profile;
	int					audio_profile;

	/* Retained diagnostics after handing off to music-only playback. */
	struct mh_race_effect_buffer_stats	finished_buffer;
	struct mh_pause_sound_assets		menu;
	struct mh_mixer				menu_mixer;
	struct mh_audio_device			*device;
	int					device_started;
	int	paused, race_bank_loaded, menu_visible;
	int					music_only;
	int	music_output_failed, music_trace, phase_trace;
	unsigned long				music_callbacks;
	unsigned long				listener_revision;

	/* This race still owns the process sound history. */
	int					owns_sound_history;
	int					manual_service, output_failed;
	char					diagnostic[MH_RACE_AUDIO_ERROR_SIZE];
};

void			mh_race_audio_banks(struct mh_audio *audio,
	struct mh_mixer *banks[MH_RACE_AUDIO_BANKS]);
double			mh_race_audio_effects_gain(unsigned int volume);
enum mh_error_code	mh_race_audio_open_device(
	const struct mh_audio_provider *provider, struct mh_audio *audio,
	struct mh_error *error_out);

/* Device pause/resume must happen without the device exclusion. */
int	mh_race_audio_pause_device(struct mh_audio *audio, int paused);

/* These operations require the device exclusion once the device is running. */
int			mh_race_audio_clear_effects(struct mh_audio *audio);
enum mh_error_code	mh_race_audio_service_effects(void *context,
	struct mh_error *error_out);
enum mh_error_code	mh_race_audio_continue_results(struct mh_audio *audio,
	struct mh_error *error_out);
void			mh_race_audio_trace_state(const struct mh_audio *audio,
	const char *event);

/*
 * Flush pending stops, check service status, then release the device exclusion.
 */
int	mh_race_audio_unlock(struct mh_audio *audio, int ok);

#endif
