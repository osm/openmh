#ifndef MH_AUDIO_RACE_SOUND_ASSETS_H
#define MH_AUDIO_RACE_SOUND_ASSETS_H

#include "engine/audio/race_sound.h"
#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_horn_source;

struct mh_resource_source;
struct mh_audio_decoder_provider;

struct mh_race_sound_assets {
	struct mh_sound_bank	bank;
	const char		*names[MH_RACE_SAMPLES];
	char			horn_samples[MH_RACE_MAX_CARS][128];
	struct mh_file		files[MH_RACE_SAMPLES];
	struct mh_file		horn_files[MH_RACE_MAX_CARS];
};

/*
 * NULL names/entries select the catalog default (prefer Honker1). Names are
 * catalog display names, in field order (MH_RACE_MAX_CARS entries when
 * non-NULL). Repeated catalog entries
 * share owned PCM. Loading errors preserve output; free an earlier bank before
 * reusing it.
 * Missing turbo/finish/elimination/message cues set bank.absent_samples;
 * required engine/countdown cues and present malformed samples remain errors.
 * Horn source, decoder and resources are borrowed only while loading.
 */
enum mh_error_code	mh_race_sound_assets_load_from(
	const struct mh_horn_source *horn_source,
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *const *horn_names, struct mh_race_sound_assets *output,
	struct mh_error *error_out);
void			mh_race_sound_assets_free(
	struct mh_race_sound_assets *assets);

#endif
