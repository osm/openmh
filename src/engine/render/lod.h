#ifndef MH_LOD_H
#define MH_LOD_H

#include "engine/core/error.h"

enum mh_lod_renderer {
	MH_LOD_SOFTWARE = 1,
	MH_LOD_HARDWARE = 2
};

/*
 * Car-body detail chain. Wheels and scenery are separate objects, not members
 * of this chain. Renderer 1 is software; renderer 2 is hardware. Zero preserves
 * the remaining original branch.
 */
struct mh_lod_input {
	unsigned int	levels, renderer;
	int		ignition, supercars, unavailable;
	float		focal, width, eye[3], position[3];
};

struct mh_lod_selection {
	unsigned int	level;

	/* Software far detail: disable reflection. */
	int		coarse;
};

/*
 * One to four contiguous bodies, with original link distances 20/40/60.
 * Equality retains the nearer body. Output is unchanged on invalid input.
 * Software rounds each squared distance component and sum to binary32.
 * Hardware accumulates in long double before rounding the distance to float.
 */
enum mh_error_code	mh_lod_select(const struct mh_lod_input *input,
	struct mh_lod_selection *output, struct mh_error *error_out);

#endif
