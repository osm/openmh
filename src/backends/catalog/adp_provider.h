#ifndef MH_CATALOG_ADP_PROVIDER_H
#define MH_CATALOG_ADP_PROVIDER_H

struct mh_profile_source;

/*
 * Static original ADP provider; keys are ADP filenames or suffix-free aliases.
 */
const struct mh_profile_source	*mh_profile_adp_source(void);

#endif
