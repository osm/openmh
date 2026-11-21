#include "engine/core/limits.h"
#include "engine/network/score.h"

enum mh_error_code
mh_net_score_add(struct mh_net_profile *profile, unsigned int position,
	struct mh_error *error_out)
{
	static const unsigned int points[MH_NET_PLAYERS] = {
		12, 10, 8, 6, 4, 3, 2, 1};

	unsigned int award;

	if (profile == NULL || position < 1 ||
		position >= MH_NET_WIRE_PLAYERS ||
		profile->values[MH_NET_PROFILE_RACES] > MH_U32_MAX ||
		profile->values[MH_NET_PROFILE_POINTS] > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network round score");

	award = position <= MH_NET_PLAYERS ? points[position - 1] : 0;
	profile->values[MH_NET_PROFILE_RACES] =
		(profile->values[MH_NET_PROFILE_RACES] + 1UL) & MH_U32_MAX;
	profile->values[MH_NET_PROFILE_POINTS] =
		(profile->values[MH_NET_PROFILE_POINTS] + award) & MH_U32_MAX;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
