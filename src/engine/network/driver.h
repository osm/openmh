#ifndef MH_NET_DRIVER_H
#define MH_NET_DRIVER_H

#include "engine/network/protocol.h"

int	mh_net_driver_text_valid(const struct mh_net_profile *profile);
void	mh_net_driver_edit(struct mh_net_profile *profile,
	const struct mh_net_profile *incoming, int allow_horn);

/* Preserve attributed emotes. An emote has an empty command body. */
enum mh_error_code	mh_net_driver_chat(const char *name, const char *text,
	char *output, size_t capacity, const char **body,
	struct mh_error *error_out);

#endif
