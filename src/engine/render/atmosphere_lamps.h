#ifndef MH_RENDER_LAMPS_H
#define MH_RENDER_LAMPS_H

#include "engine/core/error.h"
#include "engine/render/halo.h"
#include "engine/render/mesh.h"
#include "engine/render/profile.h"
#include "engine/render/world_view.h"

/* Shared world/attached source capacity, checked before allocation. */
#define MH_LAMP_LIMIT 100000
/* Each lamp submits a lens-flare draw followed by a glow draw. */
#define MH_LAMP_DRAWS 2

struct mh_image_decoder_provider;

struct mh_lamps {
	struct mh_mesh		mesh;
	enum mh_render_profile	profile;
	size_t			count, world_count;

	/* Renderer options, initially on; halos gates the entire source. */
	int			halos, flares;
	struct mh_depth_probe	*probes;

	/*
	 * Previous frame; original world records start nonzero. PowerVR ignores
	 * it.
	 */
	unsigned char		*visible;

	/* Owned cell eligibility, independent of depth history. */
	unsigned char		*world_hidden;
};

/* Glow-only mesh; empty worlds keep one degenerate GPU slot. */
enum mh_error_code	mh_halo_mesh_create(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world, const unsigned char *data, size_t size,
	struct mh_mesh *output, struct mh_error *error_out);
enum mh_error_code	mh_halo_mesh_update(struct mh_mesh *mesh,
	const struct mh_world *world, const float world_to_clip[16],
	unsigned int width, unsigned int height, float near_depth,
	float far_depth, float cue_start, struct mh_error *error_out);

enum mh_error_code	mh_lamps_create(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world, const unsigned char *glow,
	size_t glow_size, const unsigned char *hex, size_t hex_size,
	struct mh_lamps *output, struct mh_error *error_out);
void			mh_lamps_free(struct mh_lamps *lamps);

/*
 * Original cell traversal visits halos before the cell model's sphere gate.
 * NULL restores all world sources. Culled sources retain depth history.
 */
enum mh_error_code	mh_lamps_world_cull(struct mh_lamps *lamps,
	const struct mh_world *world, const struct mh_world_view *cell_view,
	struct mh_error *error_out);

/*
 * Resize before GPU upload; keeps textures and world visibility history.
 * World and attached sources share one query/readback batch.
 * Failure retains the existing allocation. Resizing clears geometry/probes;
 * requesting the current capacity is a no-op.
 */
enum mh_error_code	mh_lamps_reserve_attached(struct mh_lamps *lamps,
	size_t attached_count, struct mh_error *error_out);
enum mh_error_code	mh_lamps_update(struct mh_lamps *lamps,
	const struct mh_world *world, const float world_to_clip[16],
	unsigned int width, unsigned int height, float near_depth,
	float far_depth, float cue_start, struct mh_error *error_out);

/*
 * Updates an attached slot after mh_lamps_update cleared the frame. Model
 * may be NULL for world-space endpoints. Style direction is local; this
 * adapter transforms it to camera space and sets inverse_focal.
 * Failure preserves the slot; an out-of-view source publishes an empty slot.
 */
enum mh_error_code	mh_lamps_attached_update(struct mh_lamps *lamps,
	size_t index, const struct mh_geometry_halo *halo,
	const struct mh_attached_halo *style, const float *model_to_world,
	const float world_to_clip[16], const float world_to_view[16],
	unsigned int width, unsigned int height, float near_depth,
	float far_depth, float cue_start, struct mh_error *error_out);

#endif
