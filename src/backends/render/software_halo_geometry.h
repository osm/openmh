#ifndef MH_SOFTWARE_HALO_GEOMETRY_H
#define MH_SOFTWARE_HALO_GEOMETRY_H

#include "backends/render/software_face.h"
#include "backends/render/software_fog.h"
#include "backends/render/software_order.h"
#include "engine/assets/image.h"

#define MH_SOFTWARE_HALO_FACES 8

enum mh_software_halo_flags {
	MH_SOFTWARE_HALO_ENABLED = 2,
	MH_SOFTWARE_HALO_LENSES = 4,
	MH_SOFTWARE_HALO_DIRECTIONAL = 8,
	MH_SOFTWARE_HALO_REAR = 16,
	MH_SOFTWARE_HALO_WORLD = 32
};

/*
 * Endpoints are projected; direction has already been rotated into camera
 * space.
 */
struct mh_software_halo_source {
	unsigned int			flags;
	struct mh_software_point	endpoints[2];
	float				direction[3], intensity, size, flare;
	unsigned char			color[3];
	int				tinted;
	float				tint[3];
};

/* Borrowed images; draw the chain only after its marker passes visibility. */
struct mh_software_halo_geometry {
	float				marker[2];
	unsigned int			bucket, count;
	struct mh_software_clipped_face	faces[MH_SOFTWARE_HALO_FACES];
	const struct mh_indexed_image	*images[MH_SOFTWARE_HALO_FACES];
};

/*
 * Prepare halos after endpoint projection/direction rotation. A culled source
 * succeeds with count zero. lenses_enabled is the caller's world/car setting.
 * The supplied band retains the caller's ordering policy. Errors preserve
 * output.
 */
enum mh_error_code	mh_software_halo_geometry(
	const struct mh_software_halo_source *, const struct mh_projection *,
	const struct mh_software_fog *, const struct mh_software_depth_band *,
	const struct mh_indexed_image *glow,
	const struct mh_indexed_image *lens, int lenses_enabled,
	struct mh_software_halo_geometry *, struct mh_error *error_out);

#endif
