#ifndef MH_MENU_SOUND_H
#define MH_MENU_SOUND_H

#include "engine/audio/mixer.h"
#include "engine/core/error.h"
#include "engine/settings/menu_sound.h"
#include "interfaces/audio/horn.h"

#define MH_MENU_SOUND_BANKS 2
#define MH_MENU_HORN_BANKS ((MH_HORNS + MH_MIXER_VOICES - 1) / MH_MIXER_VOICES)
#define MH_MENU_AUDIO_BANKS (MH_MENU_SOUND_BANKS + MH_MENU_HORN_BANKS)

/*
 * One logical record per config definition, even for duplicate filenames.
 * Parsed PCM is borrowed and must outlive every active voice.
 */
struct mh_menu_sound_bank {
	struct mh_menu_sound_config	config;
	struct mh_audio_pcm		waves[MH_MENU_SOUND_SAMPLES];
};

struct mh_menu_horn_bank {
	struct mh_horn_catalog	catalog;

	/* NULL PCM represents a silent <None>. */
	struct mh_audio_pcm	waves[MH_HORNS];
};

/*
 * Nonpositional preparation: binary32(rate*pitch), equal clamped ears. This
 * menu adapter bounds pitch to .01..16; failure preserves outputs.
 */
enum mh_error_code	mh_menu_sound_parameters(
	const struct mh_menu_sound_sample *sample, unsigned long rate,
	float *frequency, float *volume, struct mh_error *error_out);

/*
 * Zero/out-of-range/unmapped events are no-ops. Samples have flags 4
 * (retrigger) and base priority 0. The two logical banks occupy consecutive
 * windows in the same pool. The caller requests any lightning first, even when
 * audio is unavailable. Audio errors preserve pool/banks.
 */
enum mh_error_code	mh_menu_sound_play(struct mh_mixer *const *banks,
	const struct mh_menu_sound_bank *bank, unsigned int event,
	struct mh_error *error_out);

/*
 * Horn records occupy four windows after the two menu-effect banks. Same
 * eight physical channels and queue; each horn retains its own retrigger
 * identity, nonpositional gain and original base priority 4.
 */
enum mh_error_code	mh_menu_horn_play(struct mh_mixer *const *banks,
	const struct mh_menu_horn_bank *bank, unsigned int index,
	struct mh_error *error_out);

#endif
