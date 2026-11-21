#ifndef MH_SOFTWARE_RASTER_H
#define MH_SOFTWARE_RASTER_H

#include <stddef.h>

#include "backends/render/software_material.h"
#include "engine/core/error.h"

/* Source RGB bytes times the supported effective WorldBrightness (0..200). */
#define MH_SOFTWARE_LIGHT_MAX (255.0f * (float)MH_SOFTWARE_BRIGHTNESS_MAX)

/* Borrowed 16-bit RGB pixels stored as explicit little-endian bytes. */
struct mh_software_surface {
	unsigned char	*pixels;
	size_t		size, pitch;
	unsigned int	width, height;
	unsigned int	bits[3], shift[3];
};

struct mh_software_palette;

/*
 * Tightly packed indexed texture, at most 256x256; palette is face shaded.
 * Indices and palette remain borrowed through each raster operation.
 * Affine or opaque perspective, viewport width <= 1023, UV in [0, size-0.5].
 * Extreme opaque gradients reuse cached steps; cutout/additive ones are
 * skipped. Sample bounds are checked before any writes.
 */
struct mh_software_texture {
	const unsigned char			*indices;
	size_t					size;
	unsigned int				width, height;
	const struct mh_software_palette	*palette;

	/* Skip index zero, independently of its RGB. */
	int					zero_transparent;
};

/*
 * Smooth fills use byte-range corner colors and ignore fog/outline.
 * Flat/textured light may reach MH_SOFTWARE_LIGHT_MAX; it saturates only after
 * fog/palette shading. Face preparation clamps smooth corners. Flat/textured
 * colors may also be negative (skid lights and particle fade). The original
 * unsigned saturation maps a negative rounded component to 255; preserve the
 * signed value through fog mixing instead of clamping early.
 */
struct mh_software_triangle {
	float					xy[3][2], uv[3][2];
	const struct mh_software_texture	*texture;
	float					color[3][3], fog_color[3], fog;
	int					outline, smooth;

	/* Packed additive RGB, always affine. */
	int					additive;

	/* Zero selects affine; 2..5 select original 4..32-pixel blocks. */
	unsigned int				perspective_shift;

	/*
	 * Positive projected corner weights; ignored by affine/cutout
	 * spans and additive drawing.
	 */
	float					perspective_weight[3];
};

/* Original cached affine pixel steps and last affine/perspective span steps. */
struct mh_software_texture_state {
	long		pixel_step[2], span_step[2];
	unsigned int	width;
};

/*
 * Zero-initialize state once per renderer; NULL uses fresh zero state.
 * Check advances supplied state on success without changing pixels. Prepare a
 * frame with a state copy, then draw from the original state in the same order.
 * Both operations preserve supplied state on failure.
 */
enum mh_error_code	mh_software_triangle_check(
	const struct mh_software_surface *surface,
	const struct mh_software_triangle *triangle,
	struct mh_software_texture_state *state, struct mh_error *error_out);

/*
 * Flat/smooth RGB, affine/cutout/additive or opaque perspective texture; no Z
 * buffer. Additive drawing takes precedence over index-zero cutouts. XY may
 * extend beyond any viewport edge. A corner outside +/-30000 rejects the
 * triangle without changing pixels or texture state. The original signed-word Y
 * order and row/span clipping remain without cuts.
 */
enum mh_error_code	mh_software_triangle(
	const struct mh_software_surface *surface,
	const struct mh_software_triangle *triangle,
	struct mh_software_texture_state *state, struct mh_error *error_out);

#endif
