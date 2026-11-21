#ifndef MH_PALETTE_ACT_PROVIDER_H
#define MH_PALETTE_ACT_PROVIDER_H

struct mh_palette_source;

/* Static original-format provider; no caller-owned state. */
const struct mh_palette_source	*mh_palette_act_source(void);

#endif
