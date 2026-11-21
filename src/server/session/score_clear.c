#include "server/session/score_clear.h"

enum mh_server_score_clear
mh_server_score_clear(struct mh_server_lobby *lobby)
{
	struct mh_net_profile *profile;
	enum mh_server_score_clear result;
	unsigned int i, player_count, votes;

	result = MH_SERVER_SCORE_FORCED;

	if (!(lobby->clear_votes & 1U)) {
		player_count = votes = 0;

		for (i = 1; i < lobby->roster.player_count; i++) {
			if (!(lobby->roster.players[i].flags &
				MH_NET_FLAG_LOBBY))
				continue;

			player_count++;

			if (lobby->clear_votes & (1U << i))
				votes++;
		}

		if (votes <= player_count / 2)
			return MH_SERVER_SCORE_UNCHANGED;

		result = MH_SERVER_SCORE_MAJORITY;
	}

	/* All configured records, including absent drivers. */
	for (i = 0; i < lobby->roster.player_count; i++) {
		profile = &lobby->roster.players[i];
		profile->values[MH_NET_PROFILE_RACES] = 0;
		profile->values[MH_NET_PROFILE_POINTS] = 0;
		profile->value_a = profile->value_b = 0;
	}

	lobby->clear_votes = 0;

	return result;
}
