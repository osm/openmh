#ifndef MH_ENGINE_AUDIO_MENU_PLAYBACK_H
#define MH_ENGINE_AUDIO_MENU_PLAYBACK_H

struct mh_horn_catalog;
struct mh_clock_source;
struct mh_resource_source;

struct mh_menu_audio;
struct mh_audio_provider;
struct mh_audio_decoder_provider;

struct mh_menu_audio_state {
	unsigned long	effects_callbacks;
	unsigned long effects_fills, effects_wraps,
		effects_frames; /* Ring writes, split writes, 22050 Hz frames
				consumed. */
	unsigned int	effects_pending, effects_channels;
	double		effects_gain, music_gain;
	unsigned int	music_volume, track;
	int		music_active;
	double		music_frame;
	unsigned int	effects_active;
};

/*
 * Selected output/scheduler and decoder, borrowed until close. NULL output
 * provider is silent unavailable audio; this constructor never selects a native
 * default. All sequencing and owned PCM stay portable; provider callbacks
 * borrow that PCM only until device close detaches them. Resources/clock run on
 * the caller. NULL on failure with a diagnostic on stderr. The supplied 0..1
 * volume becomes original integer percent, applied locally to the device PCM.
 */
struct mh_menu_audio	*mh_menu_audio_open_from(
	const struct mh_audio_provider *provider,
	const struct mh_audio_decoder_provider *decoder, const char *root,
	double volume, const struct mh_resource_source *resources,
	const struct mh_clock_source *clock);
void			 mh_menu_audio_close(struct mh_menu_audio *audio);

/*
 * Menu departure stops menu effects, retaining CD music for the network lobby.
 * Independent callbacks continue playback; close this device at race loading or
 * before rebuilding the menus.
 */
int	mh_menu_audio_leave(struct mh_menu_audio *audio);

/*
 * Menu entry/return selects MenuCDTrack at nonzero volume. Missing music
 * is reported on stderr and leaves effects usable. NULL directory uses the
 * extraction layout; the original menus does not call CD repeat.
 */
void	mh_menu_audio_music(struct mh_menu_audio *audio, const char *root,
	const char *directory, unsigned int volume, unsigned long track);

/*
 * Sound-options device endpoints. Raw 0..255 volumes use original integer
 * percent. Full effects volume preserves original device PCM; the separate
 * music bus retains native .35 gain. Volume alone never starts music;
 * the caller orders explicit stop/play before raising zero music volume.
 */
int		mh_menu_audio_effects_volume(struct mh_menu_audio *audio,
	unsigned int volume);
int		mh_menu_audio_music_volume(struct mh_menu_audio *audio,
	unsigned int volume);
unsigned long	mh_menu_audio_current_track(const struct mh_menu_audio *audio);
void		mh_menu_audio_music_stop(struct mh_menu_audio *audio);

/* Missing/invalid songs are reported and leave effects available. */
void	mh_menu_audio_music_play(struct mh_menu_audio *audio,
	unsigned int track);

/* Main-thread snapshot; NULL audio returns silence. No device types. */
int	mh_menu_audio_inspect(const struct mh_menu_audio *audio,
	struct mh_menu_audio_state *state);

/* NULL audio is a silent no-op. The caller still handles visual feedback. */
int	mh_menu_audio_play(struct mh_menu_audio *audio, unsigned int event);

/* Load once, before any horn requests. NULL audio is a silent no-op. */
int	mh_menu_audio_load_horns(struct mh_menu_audio *audio, const char *root,
	const struct mh_horn_catalog *catalog);
int	mh_menu_audio_horn(struct mh_menu_audio *audio, unsigned int index);

/*
 * Check worker health. Playback runs independently every 40 ms; this call
 * never admits requests, refreshes channels or generates PCM.
 */
int	mh_menu_audio_update(struct mh_menu_audio *audio);

#endif
