#ifndef MH_SOFTWARE_HALOS_H
#define MH_SOFTWARE_HALOS_H

#include "backends/render/software_halo_geometry.h"
#include "backends/render/software_halo_visibility.h"
#include "interfaces/render/halos.h"

struct mh_software_halo_entry {
	struct mh_software_halo_geometry	geometry;
	size_t					part, before_primitive;

	/* Deferred triangle range, assigned during scene preparation. */
	size_t					first, count;
};

/* Zero initialize once; owns scratch arrays and borrows images. */
struct mh_software_halos {
	struct mh_software_halo_entry	*entries;
	struct mh_software_halo_marker	*markers;
	size_t				*draw_order;
	size_t				count, capacity;
};

/*
 * Inputs are borrowed during preparation; images remain borrowed through
 * drawing. Errors clear count while retaining allocations; scratch entries
 * may be partly prepared. Culled entries retain their insertion positions
 * after successful preparation.
 */
enum mh_error_code	mh_software_halos_prepare(
	struct mh_software_halos *halos, const struct mh_renderer_halos *inputs,
	const struct mh_projection *projection, const float view[16],
	struct mh_error *error_out);
void			mh_software_halos_free(struct mh_software_halos *halos);

#endif
