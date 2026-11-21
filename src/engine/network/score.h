#ifndef MH_NET_SCORE_H
#define MH_NET_SCORE_H

#include "engine/network/protocol.h"

/*
 * One race plus rank points, with original 32-bit counter wrap. Ranks 1..8
 * earn 12,10,8,6,4,3,2,1; dedicated ranks 9..12 earn zero. A participant need
 * not have finished. Callers enforce their own membership and rank limits.
 * Failure preserves the profile.
 */
enum mh_error_code	mh_net_score_add(struct mh_net_profile *profile,
	unsigned int position, struct mh_error *error_out);

#endif
