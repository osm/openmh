#ifndef MH_MENU_SOUND_ASSETS_H
#define MH_MENU_SOUND_ASSETS_H

#include "engine/audio/menu_sound.h"
#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_resource_source;
struct mh_audio_decoder_provider;

struct mh_menu_sound_assets {
	struct mh_menu_sound_bank	bank;
	struct mh_file			files[MH_MENU_SOUND_SAMPLES];
};

struct mh_menu_horn_assets {
	struct mh_menu_horn_bank	bank;
	struct mh_file			files[MH_HORNS];
};

/*
 * Config plus case-insensitive sample references, at most 64 MiB of sample
 * bytes. Duplicate files share PCM but retain separate logical records.
 * Output is owned; free before reuse. Loading errors preserve output.
 * Decoder/source are borrowed only while loading.
 */
enum mh_error_code	mh_menu_sound_assets_load_from(
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	struct mh_menu_sound_assets *output, struct mh_error *error_out);
void			mh_menu_sound_assets_free(
	struct mh_menu_sound_assets *assets);

/*
 * Supplied catalog fixes selection order. PCM has independent 64 MiB bound;
 * duplicate filenames share owned bytes, logical records remain distinct.
 * Decoder/source are borrowed only while loading.
 */
enum mh_error_code	mh_menu_horn_assets_load_from(
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const struct mh_horn_catalog *catalog,
	struct mh_menu_horn_assets *output, struct mh_error *error_out);
void			mh_menu_horn_assets_free(
	struct mh_menu_horn_assets *assets);

#endif
