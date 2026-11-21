#ifndef MH_HALO_H
#define MH_HALO_H

#include "engine/core/error.h"
#include "engine/render/effect_quad.h"

enum mh_halo_flag {
	MH_HALO_GLOW = 2,
	MH_HALO_FLARES = 4,
	MH_HALO_DIRECTIONAL = 8,
	MH_HALO_REAR = 16,
	MH_HALO_WORLD = 32,
	MH_HALO_GLOW_FLARES = MH_HALO_GLOW | MH_HALO_FLARES,
	MH_HALO_HEADLIGHT = MH_HALO_GLOW_FLARES | MH_HALO_DIRECTIONAL,
	MH_HALO_TAILLIGHT = MH_HALO_GLOW | MH_HALO_DIRECTIONAL | MH_HALO_REAR
};

enum {
	MH_WORLD_GLOW_QUADS = 2,
	MH_LENS_QUADS = 7,
	MH_WORLD_LAMP_QUADS = MH_WORLD_GLOW_QUADS + MH_LENS_QUADS,
	MH_ATTACHED_LAMP_QUADS = 1 + MH_LENS_QUADS
};

struct mh_halo_projection {
	/* Pixel XY, view Z/far, reciprocal view Z. */
	float		endpoint[2][4];
	float	width, height, pixel_aspect, near_depth, far_depth, cue_start;
	unsigned char	color[3];
};

/*
 * Per-source depth query. Pixel coordinates are truncated by the backend.
 * The core emits reciprocal near/Z; mesh adapters convert to GPU depth.
 */
struct mh_depth_probe {
	int	active;
	float	x, y, depth;
};

/*
 * Attached source inputs. Direction is in camera space (positive Z forward);
 * inverse_focal is 1/horizontal focal. Scenery uses its animated RGB when
 * tinted, and bypasses the flare toggle. Only the single-glow flags
 * 2/6/0xe/0x1a are accepted.
 */
struct mh_attached_halo {
	unsigned int	flags;
	float		intensity, size, flare;
	float		direction[3], inverse_focal;
	int		scenery, tinted;
	float		tint[3];
};

/*
 * Two original world-lamp glow layers (flags 0x26). Invisible returns two
 * zero quads. Depth is the original reciprocal near/Z; UV addresses the
 * 256px atlas, with halofix in its upper-left 128px square.
 */
enum mh_error_code	mh_world_halo_quads(
	const struct mh_halo_projection *projection,
	struct mh_effect_quad output[MH_WORLD_GLOW_QUADS],
	struct mh_error *error_out);

/*
 * Hardware visibility history plus two glows and seven lens reflections.
 * The query is emitted even when the previous frame hid the source.
 */
enum mh_error_code	mh_world_lamp_quads(
	const struct mh_halo_projection *projection, int previous_visible,
	int flares, struct mh_depth_probe *probe,
	struct mh_effect_quad output[MH_WORLD_LAMP_QUADS],
	struct mh_error *error_out);

/* One glow and seven lens reflections. Output/probe unchanged on error. */
enum mh_error_code	mh_attached_halo_quads(
	const struct mh_halo_projection *projection,
	const struct mh_attached_halo *style, int previous_visible, int flares,
	struct mh_depth_probe *probe,
	struct mh_effect_quad output[MH_ATTACHED_LAMP_QUADS],
	struct mh_error *error_out);

#endif
