#ifndef MH_SOFTWARE_NAME_GEOMETRY_H
#define MH_SOFTWARE_NAME_GEOMETRY_H

#include "backends/render/software_face.h"
#include "backends/render/software_order.h"
#include "engine/assets/image.h"

struct mh_software_name_source {
	float				world[3], camera[3], height, size[2];
	const struct mh_indexed_image	*image;
	int				transparent;
};

struct mh_software_name_geometry {
	struct mh_software_clipped_face	face;
	unsigned int			bucket;
};

/*
 * Positions are the body origin in world/camera space; view projects its
 * elevated label anchor. The caller supplies the label depth band (original
 * near margin 0, far margin 10). Image storage is borrowed. A culled label
 * succeeds with face.count zero; errors preserve output.
 */
enum mh_error_code	mh_software_name_project(
	const struct mh_software_name_source *, const struct mh_projection *,
	const float view[12], const struct mh_software_depth_band *,
	struct mh_software_name_geometry *, struct mh_error *error_out);

#endif
