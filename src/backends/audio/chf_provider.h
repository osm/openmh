#ifndef MH_AUDIO_CHF_PROVIDER_H
#define MH_AUDIO_CHF_PROVIDER_H

struct mh_horn_source;

/* Static original CHF catalog provider; no caller-owned state. */
const struct mh_horn_source	*mh_horn_chf_source(void);

#endif
