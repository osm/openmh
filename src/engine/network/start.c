#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/network/start.h"

#define PLAYER_MASK ((1U << MH_NET_PLAYERS) - 1U)
#define HOST_MASK 1U
#define MIN_SIGNAL_DELAY_MS 1000UL
#define START_DELAY_MS 2000UL
#define GRID_RETRY_LIMIT 65536UL

void
mh_net_start_reset(struct mh_net_start *scheduler)
{
	if (scheduler != NULL)
		memset(scheduler, 0, sizeof(*scheduler));
}

static enum mh_error_code
start_step(enum mh_net_wire wire, int dedicated, struct mh_net_start *scheduler,
	unsigned int active_mask, unsigned int ready_mask,
	const unsigned long *ping_ms, long double now_ms,
	unsigned int *send_mask, struct mh_error *error_out)
{
	struct mh_net_start next;
	unsigned long earliest_signal_ms, start_deadline_ms, peer_deadline_ms;
	unsigned int i, due_mask, bit, capacity, player_mask, remote_mask;
	int patched, start_due;

	capacity = dedicated ? MH_NET_WIRE_PLAYERS : MH_NET_PLAYERS;
	player_mask = (1U << capacity) - 1;
	remote_mask = player_mask & ~HOST_MASK;
	patched = wire == MH_NET_WIRE_31_LOBBY;

	if ((wire != MH_NET_WIRE_F12 && wire != MH_NET_WIRE_31_LOBBY) ||
		scheduler == NULL || ping_ms == NULL || send_mask == NULL ||
		active_mask > player_mask || !(active_mask & HOST_MASK) ||
		ready_mask > player_mask || !(now_ms >= scheduler->now_ms) ||
		!(now_ms >= 0 && now_ms < MH_U32_MODULUS))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network start context");

	for (i = 0; i < capacity; i++)
		if (ping_ms[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid start round trip time");

	next = *scheduler;
	next.now_ms = now_ms;
	due_mask = 0;

	if ((active_mask & ~ready_mask &
		(patched && !dedicated ? player_mask : remote_mask)) == 0 &&
		next.anchor_ms == 0)
		next.anchor_ms = (unsigned long)now_ms;

	if (next.anchor_ms != 0) {
		earliest_signal_ms =
			(next.anchor_ms + MIN_SIGNAL_DELAY_MS) & MH_U32_MAX;
		start_deadline_ms =
			(next.anchor_ms + START_DELAY_MS) & MH_U32_MAX;

		for (i = patched && !dedicated ? 0 : 1; i < capacity; i++) {
			bit = 1U << i;

			if (!(active_mask & bit) || (next.sent_mask & bit))
				continue;

			peer_deadline_ms =
				(start_deadline_ms - (ping_ms[i] >> 1)) &
				MH_U32_MAX;

			if (now_ms > earliest_signal_ms &&
				now_ms > peer_deadline_ms)
				due_mask |= bit;
		}

		next.sent_mask |= due_mask;

		/*
		 * Patched and dedicated servers: first due signal starts time.
		 */
		start_due =
			patched ? due_mask != 0 : now_ms > start_deadline_ms;

		if (start_due && next.started_ms == 0)
			next.started_ms = (unsigned long)now_ms;
	}

	*scheduler = next;
	*send_mask = due_mask & remote_mask;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_start_step_for(enum mh_net_wire wire, struct mh_net_start *scheduler,
	unsigned int active_mask, unsigned int ready_mask,
	const unsigned long ping_ms[MH_NET_PLAYERS], long double now_ms,
	unsigned int *send_mask, struct mh_error *error_out)
{
	return start_step(wire, 0, scheduler, active_mask, ready_mask, ping_ms,
		now_ms, send_mask, error_out);
}

enum mh_error_code
mh_net_start_step_dedicated(struct mh_net_start *scheduler,
	unsigned int active_mask, unsigned int ready_mask,
	const unsigned long ping_ms[MH_NET_WIRE_PLAYERS], long double now_ms,
	unsigned int *send_mask, struct mh_error *error_out)
{
	return start_step(MH_NET_WIRE_31_LOBBY, 1, scheduler, active_mask,
		ready_mask, ping_ms, now_ms, send_mask, error_out);
}

enum mh_error_code
mh_net_start_step(struct mh_net_start *scheduler, unsigned int active_mask,
	unsigned int ready_mask, const unsigned long ping_ms[MH_NET_PLAYERS],
	long double now_ms, unsigned int *send_mask, struct mh_error *error_out)
{
	return mh_net_start_step_for(MH_NET_WIRE_F12, scheduler, active_mask,
		ready_mask, ping_ms, now_ms, send_mask, error_out);
}

enum mh_error_code
mh_net_grid(unsigned int active_mask, unsigned long *seed,
	unsigned int positions[MH_NET_PLAYERS], struct mh_error *error_out)
{
	unsigned int ranks[MH_NET_PLAYERS], player, other, player_count;
	unsigned long random_state, retries, draw;

	if (active_mask == 0 || active_mask > PLAYER_MASK || seed == NULL ||
		positions == NULL || *seed > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network grid arguments");

	random_state = *seed;
	player_count = 0;
	retries = 0;
	memset(ranks, 0, sizeof(ranks));

	for (player = 0; player < MH_NET_PLAYERS; player++)
		if (active_mask & (1U << player))
			player_count++;

	for (player = 0; player < MH_NET_PLAYERS; player++)
		if (active_mask & (1U << player)) {
			draw = mh_random_next(&random_state);
			ranks[player] = (unsigned int)(draw % player_count);
		}

	for (player = 0; player < MH_NET_PLAYERS; player++) {
		if (!(active_mask & (1U << player)))
			continue;

		other = 0;

		while (other < MH_NET_PLAYERS) {
			if (player != other && (active_mask & (1U << other)) &&
				ranks[player] == ranks[other]) {
				/*
				 * Bound the original repeated collision scan.
				 */
				if (++retries > GRID_RETRY_LIMIT)
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"network grid randomization exhausted");

				draw = mh_random_next(&random_state);
				ranks[player] =
					(unsigned int)(draw % player_count);
				other = 0;
			} else
				other++;
		}
	}

	for (player = 0; player < MH_NET_PLAYERS; player++)
		if (active_mask & (1U << player))
			ranks[player]++;

	*seed = random_state;
	memcpy(positions, ranks, sizeof(ranks));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
