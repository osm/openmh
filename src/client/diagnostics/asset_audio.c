#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/material/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_audio.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_ambient_assets_load(const char *root, const char *esd, const char *scene,
	struct mh_ambient_assets *output, struct mh_error *error_out)
{
	return mh_ambient_assets_load_from(mh_env_sounds_esd_source(),
		mh_animation_lightwave_source(), mh_audio_fallback_decoder(),
		mh_filesystem_resources(), root, esd, scene, output, error_out);
}

enum mh_error_code
mh_race_sound_assets_load(const char *root, const char *const *horn_names,
	struct mh_race_sound_assets *output, struct mh_error *error_out)
{
	return mh_race_sound_assets_load_from(mh_horn_fallback_source(),
		mh_audio_fallback_decoder(), mh_filesystem_resources(), root,
		horn_names, output, error_out);
}

enum mh_error_code
mh_surface_assets_load(const char *root, const char *srf, const char *esd,
	struct mh_surface_assets *output, struct mh_error *error_out)
{
	return mh_surface_assets_load_from(mh_material_mat_srf_source(),
		mh_env_sounds_esd_source(), mh_audio_fallback_decoder(),
		mh_filesystem_resources(), root, srf, esd, output, error_out);
}

enum mh_error_code
mh_music_init(const char *root, const char *directory, unsigned int volume,
	int repeat, struct mh_music *music, struct mh_error *error_out)
{
	return mh_music_init_from(mh_audio_fallback_decoder(),
		mh_filesystem_resources(), root, directory, volume, repeat,
		music, error_out);
}
