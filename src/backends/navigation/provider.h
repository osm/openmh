#ifndef MH_NAVIGATION_PROVIDER_H
#define MH_NAVIGATION_PROVIDER_H

#include "interfaces/navigation/source.h"

/* Process-lifetime MOT route/camera and DAT AI path readers. */
const struct mh_navigation_source	*mh_navigation_mot_dat_source(void);

/*
 * Route-only source requiring TBL-wrapped MOT; no camera/AI linkage.
 */
const struct mh_navigation_source	*mh_navigation_tbl_routes(void);

#endif
