#ifndef MH_ASSETS_MYW_H
#define MH_ASSETS_MYW_H

#include "engine/assets/myo.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/render/lighting.h"
#include "interfaces/geometry/world.h"

enum mh_myw_block_index {
	MH_MYW_MODELS,
	MH_MYW_VERTICES,
	MH_MYW_SCRATCH,
	MH_MYW_PRIMITIVES,
	MH_MYW_PLANES,
	MH_MYW_NORMALS,
	MH_MYW_TEXTURES,
	MH_MYW_UV,
	MH_MYW_LIGHTS,
	MH_MYW_LINKS,
	MH_MYW_NAMES,
	MH_MYW_AUXILIARY,
	MH_MYW_BLOCK_COUNT
};

/*
 * Borrowed, immutable world view; buffer must remain alive and unchanged.
 * Scenery and collision are separate LWSC and COL assets.
 */
struct mh_myw {
	const unsigned char	*data;
	struct mh_myo_block	blocks[MH_MYW_BLOCK_COUNT];
	unsigned long		width;
	unsigned long		height;
	size_t			grid_offset;
	size_t			cell_count;

	/* X/Z spacing, rounded to binary32 by the loader. */
	double			cell_size[2];

	/* Stored header origin, distinct from vertex bounds. */
	double			grid_minimum[3];
	unsigned long		primitive_types[MH_MYO_PRIMITIVE_TYPES];
	double			minimum[3];
	double			maximum[3];
};

#define MH_MYW_NO_MODEL MH_U32_MAX

struct mh_myw_cell {
	/* Descriptor index, or MH_MYW_NO_MODEL. */
	unsigned long	model;
	unsigned long	halo_first, halo_count;
};

#define MH_MYW_NO_LIGHT MH_U32_MAX

/*
 * Validates geometry and all accessed spans/indices; output unchanged on
 * failure. Supports the contiguous model slices in the shipped MYW files.
 */
enum mh_error_code	mh_myw_parse(const unsigned char *data, size_t size,
	struct mh_myw *world, struct mh_error *error_out);

/*
 * A bounded MYO-compatible view into the world pools, with shared texture
 * names/indirection. Empty descriptors are valid. Never relocates disk bytes.
 * world must be the result of mh_myw_parse; output unchanged on failure.
 */
enum mh_error_code	mh_myw_model(const struct mh_myw *world,
	unsigned long index, struct mh_myo *model, struct mh_error *error_out);

/* Require a parsed immutable world; failed lookups preserve all outputs. */
int	mh_myw_cell(const struct mh_myw *world, size_t index,
	struct mh_myw_cell *cell);
int	mh_myw_model_bounds(const struct mh_myw *world, unsigned long index,
	float center[3], float *radius);

/*
 * Accessors require a successfully parsed immutable world. Failed indexed
 * lookups return 0 without changing output. Light list cursors are relative
 * byte offsets, and may only come from head/begin/next (or the end sentinel).
 */
int	mh_myw_light(const struct mh_myw *world, unsigned long index,
	struct mh_point_light *light);

/*
 * Copy header RGB, intensity and direction. No normalization, color scaling or
 * ambient substitution. Requires a parsed immutable world; invalid/nonfinite
 * header values leave output unchanged.
 */
int	mh_myw_directional(const struct mh_myw *world,
	struct mh_directional_light *light);

/* First light-list cursor for an already selected grid cell. */
int			mh_myw_light_head(const struct mh_myw *world,
	size_t cell, unsigned long *cursor);
enum mh_error_code	mh_myw_lights_begin(const struct mh_myw *world,
	const double position[3], unsigned long *cursor,
	struct mh_error *error_out);
int			mh_myw_light_next(const struct mh_myw *world,
	unsigned long *cursor, struct mh_point_light *light);

/*
 * World halo inputs; resource is an original effect-table index, not a PDI
 * image index. Native world glows use the shared halofix texture; texture
 * loading lives in engine/render/scene; projection and geometry live in
 * engine/render/common/halo.
 */
int	mh_myw_halo(const struct mh_myw *world, unsigned long index,
	struct mh_geometry_halo *halo);

/* Independently owned decoded data; output is unchanged on failure. */
enum mh_error_code	mh_world_myw_decode(const unsigned char *data,
	size_t size, struct mh_world *world, struct mh_error *error_out);

#endif
