#ifndef MH_SOFTWARE_NAMES_H
#define MH_SOFTWARE_NAMES_H

#include "backends/render/software_name_geometry.h"
#include "engine/render/name_label.h"
#include "interfaces/render/names.h"

struct mh_software_name_entry {
	struct mh_software_name_geometry	geometry;
	const struct mh_indexed_image		*image;
	size_t					part;
};

/* Fixed roster scratch; images remain borrowed for the frame. */
struct mh_software_names {
	struct mh_software_name_entry	entries[MH_NAME_LABELS_MAX];
	size_t				count;
};

/* Errors clear count; entries may be partly prepared. No allocation. */
enum mh_error_code	mh_software_names_prepare(
	struct mh_software_names *output, const struct mh_renderer_names *names,
	const struct mh_projection *projection, const float view[16],
	struct mh_error *error_out);

#endif
