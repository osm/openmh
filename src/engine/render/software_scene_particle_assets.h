#ifndef MH_SOFTWARE_PARTICLE_ASSETS_H
#define MH_SOFTWARE_PARTICLE_ASSETS_H

#include "engine/assets/image.h"
#include "engine/simulation/particles.h"

struct mh_resource_source;
struct mh_image_decoder_provider;

#define MH_SOFTWARE_PARTICLE_FRAMES 20

struct mh_software_particle_animation {
	unsigned int		count;
	struct mh_indexed_image	frames[MH_SOFTWARE_PARTICLE_FRAMES];
};

struct mh_software_particle_assets {
	/* Indexed by mh_particle_animation_id minus one. */
	struct mh_software_particle_animation
		animations[MH_PARTICLE_ANIMATIONS];
};

/*
 * Original software animations: 17 smoke, grass and gravel frames, 20 sparks.
 * Every frame retains its own palette and indices; no shared hardware atlas.
 * Resources and decoder are borrowed during loading.
 * Supply empty output, free before reload. Output changes only on success.
 */
enum mh_error_code	mh_software_particle_assets_load_from(
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_software_particle_assets *output, struct mh_error *error_out);
void			mh_software_particle_assets_free(
	struct mh_software_particle_assets *assets);

#endif
