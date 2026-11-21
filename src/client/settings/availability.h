#ifndef MH_CLIENT_SETTINGS_AVAILABILITY_H
#define MH_CLIENT_SETTINGS_AVAILABILITY_H

struct mh_settings;

/* Apply after loading saved preferences, never to explicit race requests. */
void	mh_settings_apply_availability(struct mh_settings *settings);

#endif
