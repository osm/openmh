#include <math.h>
#include <string.h>

#include "server/session/ready.h"

#define CLOCK_LIMIT 1e12
#define COUNTDOWN_MS 20000.0
#define SECONDS_PER_MILLISECOND 0.001

enum {
	FIRST_NOTICE_SECONDS = 10,
	FINAL_NOTICE_SECONDS = 5
};

struct lobby_counts {
	unsigned int	present;
	unsigned int	ready;
	unsigned int	passing;
};

static void
count_players(const struct mh_server_lobby *lobby, struct lobby_counts *counts)
{
	unsigned int i, flags;

	memset(counts, 0, sizeof(*counts));

	/* Include the internal ready server slot. */
	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++) {
		flags = lobby->roster.players[i].flags;

		if (!(flags & MH_NET_FLAG_LOBBY))
			continue;

		counts->present++;

		if (flags & MH_NET_FLAG_READY)
			counts->ready++;

		if (flags & MH_NET_FLAG_PASS)
			counts->passing++;
	}
}

enum mh_error_code
mh_server_ready_command(struct mh_server_ready *ready,
	struct mh_server_lobby *lobby, const struct mh_server_config *config,
	unsigned int player, enum mh_server_ready_command command,
	struct mh_server_ready_events *events, struct mh_error *error_out)
{
	struct lobby_counts counts;
	unsigned int flags, readiness;

	if (ready == NULL || lobby == NULL || config == NULL ||
		events == NULL || player == 0 ||
		player >= MH_NET_WIRE_PLAYERS ||
		(command != MH_SERVER_READY && command != MH_SERVER_NOT_READY))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated readiness command");

	flags = lobby->roster.players[player].flags;

	if (!(flags & MH_NET_FLAG_LOBBY))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"dedicated readiness requires a lobby player");

	/* Both controls move through uncommitted. */
	if (command == MH_SERVER_READY)
		readiness = flags & MH_NET_FLAG_PASS ? 0 : MH_NET_FLAG_READY;
	else
		readiness = flags & MH_NET_FLAG_READY ? 0 : MH_NET_FLAG_PASS;

	lobby->roster.players[player].flags =
		(flags & ~(MH_NET_FLAG_READY | MH_NET_FLAG_PASS)) | readiness;
	memset(events, 0, sizeof(*events));

	if (command == MH_SERVER_NOT_READY && (flags & MH_NET_FLAG_READY) &&
		ready->deadline_ms != 0 && !config->all_ready) {
		count_players(lobby, &counts);

		if (counts.ready <= config->min_ready) {
			ready->deadline_ms = 0;
			events->countdown_cancelled = 1;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_ready_force(struct mh_server_ready *ready, double now_ms,
	struct mh_error *error_out)
{
	if (ready == NULL || !(now_ms >= 0) || now_ms > CLOCK_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated forced countdown");

	/* Replace the deadline without resetting the last announcement. */
	ready->deadline_ms = floor(now_ms + COUNTDOWN_MS);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
schedule_start(struct mh_server_ready *ready,
	const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, double now_ms,
	struct mh_server_ready_events *events)
{
	struct lobby_counts counts;
	unsigned int committed;

	count_players(lobby, &counts);
	committed = counts.ready + counts.passing;

	if (counts.ready <= 1 || committed <= config->min_ready)
		return;

	if (committed == counts.present) {
		ready->start_requested = 1;
		return;
	}

	if (!config->all_ready && counts.ready == config->min_ready + 1 &&
		ready->deadline_ms == 0) {
		ready->deadline_ms = floor(now_ms + COUNTDOWN_MS);
		events->countdown_started = 1;
	}
}

static void
advance_countdown(struct mh_server_ready *ready, double now_ms,
	struct mh_server_ready_events *events)
{
	long second;

	if (ready->deadline_ms == 0)
		return;

	/* Preserve the original double constant and extended intermediate. */
	second = (long)(((long double)ready->deadline_ms - now_ms) *
		(long double)SECONDS_PER_MILLISECOND);

	if ((second == FIRST_NOTICE_SECONDS ||
		(second >= 1 && second <= FINAL_NOTICE_SECONDS)) &&
		second != ready->last_second) {
		ready->last_second = second;
		events->countdown_second = (unsigned int)second;
	}

	if (now_ms > ready->deadline_ms)
		ready->start_requested = 1;
}

enum mh_error_code
mh_server_ready_tick(struct mh_server_ready *ready,
	const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, double now_ms,
	struct mh_server_ready_events *events, struct mh_error *error_out)
{
	if (ready == NULL || lobby == NULL || config == NULL ||
		events == NULL || config->min_ready > MH_SERVER_RACERS ||
		!(now_ms >= 0) || now_ms > CLOCK_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated readiness tick");

	memset(events, 0, sizeof(*events));

	/* Do not cancel on a lost peer here. */
	schedule_start(ready, lobby, config, now_ms, events);
	advance_countdown(ready, now_ms, events);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
