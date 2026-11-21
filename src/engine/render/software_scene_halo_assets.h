#ifndef MH_SOFTWARE_HALO_ASSETS_H
#define MH_SOFTWARE_HALO_ASSETS_H

#include "engine/assets/image.h"
#include "engine/catalog/halo_paths.h"

struct mh_resource_source;
struct mh_image_decoder_provider;

struct mh_software_halo_assets {
	unsigned int		count;
	struct mh_indexed_image	images[MH_HALO_TEXTURES];
};

/*
 * Owned indexed images, in the configuration's resource-index order.
 * World/scenery use the track bank; each car uses its own CAR bank.
 * Resources and decoder are borrowed during loading.
 * Supply empty output, free before reload. Failure leaves output unchanged.
 */
enum mh_error_code	mh_software_halo_assets_load_from(
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_halo_paths *paths,
	struct mh_software_halo_assets *output, struct mh_error *error_out);
void			mh_software_halo_assets_free(
	struct mh_software_halo_assets *assets);

/* Shared software lens image (Data/hex.iff), owned independently of banks. */
enum mh_error_code	mh_software_lens_image_load_from(
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_indexed_image *output, struct mh_error *error_out);

#endif
