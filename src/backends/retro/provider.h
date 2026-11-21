#ifndef MH_RETRO_PROVIDER_H
#define MH_RETRO_PROVIDER_H

struct mh_retro_source;

/* External parts first; absent parts fall back to a verified executable. */
const struct mh_retro_source	*mh_retro_fallback_source(void);

#endif
