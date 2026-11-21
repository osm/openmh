#ifndef MH_CLIENT_HOSTS_STORAGE_H
#define MH_CLIENT_HOSTS_STORAGE_H

struct mh_user_data;

/* Selected filesystem provider, including its user root; NULL if absent. */
const struct mh_user_data	*mh_host_user_data(void);

#endif
