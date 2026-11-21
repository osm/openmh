#ifndef MH_SOFTWARE_HALO_VISIBILITY_H
#define MH_SOFTWARE_HALO_VISIBILITY_H

#include "backends/render/software_raster.h"

/*
 * Frame-local marker, placed in ordinary bucket order before nearer geometry.
 */
struct mh_software_halo_marker {
	size_t		offset;
	unsigned int	saved;
	int		active, visible;
};

/*
 * Quantize XY, save a packed pixel and replace it with 0x7ff. A coordinate
 * whose rounded pixel lies outside the image produces an inactive marker. This
 * avoids the original unchecked edge write. Errors preserve both the surface
 * and output. RGB555 and RGB565 use the same marker word.
 */
enum mh_error_code	mh_software_halo_mark(struct mh_software_surface *,
	float x, float y, struct mh_software_halo_marker *output,
	struct mh_error *error_out);

/*
 * Resolve and restore in marker insertion order, after all ordinary geometry
 * and before ANY halo drawing. Callers then draw visible sources' glow/lens
 * chains in that same order. Overlapping markers deliberately observe preceding
 * restorations. No previous-frame history or depth buffer.
 */
enum mh_error_code	mh_software_halo_resolve(struct mh_software_surface *,
	struct mh_software_halo_marker *markers, size_t count,
	struct mh_error *error_out);

#endif
