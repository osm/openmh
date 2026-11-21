#ifndef MH_SERVER_BANS_H
#define MH_SERVER_BANS_H

#include "interfaces/network/filter.h"
#include "server/config/config.h"

struct mh_server_ban {
	char	address[MH_SERVER_BAN_ADDRESS_BYTES];

	/* Unwrapped transport time; zero means permanent. */
	double	expires_ms;
};

struct mh_server_bans {
	struct mh_server_ban	entries[MH_SERVER_BANS];
};

void	mh_server_bans_init(struct mh_server_bans *bans,
	const struct mh_server_config *config);

/* Zero duration is permanent. A full table leaves it unchanged. */
enum mh_error_code	mh_server_bans_add(struct mh_server_bans *bans,
	const char *address, unsigned long duration_ms, double now_ms,
	struct mh_error *error_out);
void			mh_server_bans_expire(struct mh_server_bans *bans,
	double now_ms);
int			mh_server_bans_contains(
	const struct mh_server_bans *bans, const char *address);

/* Supplies the borrowed, portable connection filter for this mutable table. */
void	mh_server_bans_filter(struct mh_server_bans *bans,
	struct mh_transport_filter *filter);

#endif
