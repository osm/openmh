#ifndef MH_ENV_SOUND_SOURCE_ESD_PROVIDER_H
#define MH_ENV_SOUND_SOURCE_ESD_PROVIDER_H

struct mh_env_sound_source;

/* Borrow the process-lifetime ESD definition reader. */
const struct mh_env_sound_source	*mh_env_sounds_esd_source(void);

#endif
