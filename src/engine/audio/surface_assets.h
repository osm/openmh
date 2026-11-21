#ifndef MH_AUDIO_SURFACE_ASSETS_H
#define MH_AUDIO_SURFACE_ASSETS_H

#include "engine/audio/surface.h"
#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_env_sound_source;
struct mh_material_source;
struct mh_resource_source;
struct mh_audio_decoder_provider;

#define MH_SURFACE_SAMPLE_NAME_BYTES 64

struct mh_surface_assets {
	struct mh_surface_bank	bank;
	char			names[MH_SURFACE_SAMPLES][MH_SURFACE_SAMPLE_NAME_BYTES];
	struct mh_file		files[MH_SURFACE_SAMPLES];
};

/*
 * Selected material definitions keep collision-surface order. Deduplicate
 * samples by case-insensitive basename, bounded to 64 MiB and mono 16-bit PCM.
 * Optional environment definitions supply object crash classes; built-in car
 * crashes always load.
 * Missing object-only collision samples remain registered but silent. Fixed
 * and material samples remain required; malformed present samples remain
 * errors. Owns PCM behind the borrowed bank views. Free before reuse; errors
 * preserve output and release all partial allocations. All providers are
 * borrowed only while loading.
 */
enum mh_error_code	mh_surface_assets_load_from(
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *srf, const char *esd, struct mh_surface_assets *output,
	struct mh_error *error_out);
void			mh_surface_assets_free(
	struct mh_surface_assets *assets);

#endif
