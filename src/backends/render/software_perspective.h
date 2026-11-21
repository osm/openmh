#ifndef MH_SOFTWARE_PERSPECTIVE_H
#define MH_SOFTWARE_PERSPECTIVE_H

#include "backends/render/software_raster.h"
#include "interfaces/render/renderer.h"

/*
 * Private original U*weight, V*weight and weight planes; inputs are validated.
 */
struct mh_software_perspective {
	float		source[3][3], dx[3], dy[3], block_step[3], row_step[3];
	double		value[3];
	unsigned int	block;
	int		empty;
};

/* Positive finite reciprocal camera depths; cutouts bypass this policy. */
int	mh_software_perspective_needed(enum mh_texture_mapping mapping,
	int flagged, const float weights[3]);

void	mh_software_perspective_init(struct mh_software_perspective *state,
	const struct mh_software_triangle *triangle);
void	mh_software_perspective_edge(struct mh_software_perspective *state,
	const struct mh_software_triangle *triangle, unsigned int corner,
	long x, long step, long y);
void	mh_software_perspective_advance(struct mh_software_perspective *state,
	int carry);

/*
 * Validation and drawing follow the same samples; write_pixels controls stores.
 * Cached texture steps update in either mode; validate with a state copy
 * before drawing from the original state.
 */
enum mh_error_code	mh_software_perspective_span(
	const struct mh_software_perspective *state,
	const struct mh_software_surface *surface,
	const struct mh_software_texture *texture, long low, long high, long y,
	int write_pixels, struct mh_software_texture_state *texture_state,
	struct mh_error *error_out);

#endif
