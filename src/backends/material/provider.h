#ifndef MH_MATERIAL_SOURCE_PROVIDER_H
#define MH_MATERIAL_SOURCE_PROVIDER_H

struct mh_material_source;

/* Borrow the process-lifetime MAT/SRF definition reader. */
const struct mh_material_source	*mh_material_mat_srf_source(void);

#endif
