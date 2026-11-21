#ifndef MH_CATALOG_PROVIDER_H
#define MH_CATALOG_PROVIDER_H

struct mh_catalog_source;

/* Borrow file-first catalogs with verified demo record fallback. */
const struct mh_catalog_source	*mh_catalog_fallback_source(void);

struct mh_profile_source;

/*
 * Supplied ADP profiles first; known demo layouts provide their fixed roster.
 */
const struct mh_profile_source	*mh_profile_fallback_source(void);

#endif
