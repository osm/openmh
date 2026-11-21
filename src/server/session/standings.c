#include <string.h>

#include "engine/game/standings_order.h"
#include "server/session/standings.h"

enum mh_error_code
mh_server_standings_rank(const struct mh_server_lobby *lobby,
	struct mh_server_result results[MH_NET_WIRE_PLAYERS],
	struct mh_server_standings *output, struct mh_error *error_out)
{
	struct mh_standing_key keys[MH_NET_STATISTICS_ROWS];
	struct mh_standing_order order;
	struct mh_server_standings ranked;
	unsigned int slots[MH_NET_STATISTICS_ROWS], i, count, slot;
	enum mh_error_code status;

	if (lobby == NULL || results == NULL || output == NULL ||
		lobby->roster.player_count < 2 ||
		lobby->roster.player_count > MH_NET_WIRE_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated standings roster");

	count = 0;

	for (i = 1; i < lobby->roster.player_count; i++) {
		if (!(lobby->roster.players[i].flags & MH_NET_FLAG_RACING))
			continue;

		slots[count] = i;
		keys[count].score = results[i].progress;
		keys[count].total_ms = results[i].elapsed_ms;
		keys[count].finished = results[i].finished;
		count++;
	}

	status = mh_standings_order(keys, count, &order, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&ranked, 0, sizeof(ranked));
	ranked.count = count;
	ranked.finished_count = order.finished_count;

	for (i = 0; i < count; i++) {
		slot = slots[order.entries[i]];
		ranked.order[i] = slot;
		results[slot].position = i + 1;
	}

	*output = ranked;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
