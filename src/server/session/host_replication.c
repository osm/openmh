#include <math.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/network/snapshot_schedule.h"
#include "engine/network/vehicle_message.h"
#include "server/session/host_internal.h"

#define HEARTBEAT_MS 1000UL
#define SECOND_MS 1000UL
/* Fixed dedicated distance 150 / 1.3, independent of client TRK. */
#define VIEW_DISTANCE 150.0

static enum mh_error_code
expire_racers(struct mh_server_host *host, struct mh_error *error_out)
{
	unsigned long seconds;
	unsigned int i, flags;
	double now;
	enum mh_error_code status;

	now = mh_transport_now(host->transport);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		flags = host->lobby.roster.players[i].flags;

		if (!(flags & MH_NET_FLAG_RACING))
			continue;

		seconds = flags & MH_NET_FLAG_READY
			? host->config->race_timeout_seconds
			: host->config->start_timeout_seconds;

		if (now - host->lobby.contact_ms[i] <=
			(double)seconds * SECOND_MS)
			continue;

		status = mh_server_host_disconnect(
			host, host->lobby.peers[i], " timed out!", error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
race_status(const struct mh_server_host *host, unsigned int recipient,
	double game_ms, struct mh_net_message *message)
{
	struct mh_net_status *status;
	unsigned int i;
	int unavailable;

	memset(message, 0, sizeof(*message));
	message->type = MH_NET_STATUS;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		status = &message->status[i];
		status->flags = host->lobby.roster.players[i].flags &
			MH_NET_PROFILE_FLAGS;
		status->ping_ms = host->lobby.ping_ms[i];
		status->value_a = host->race.results[i].position;
		status->value_b = (unsigned int)host->race.results[i].lap;

		if (!(status->flags & MH_NET_FLAG_RACING))
			continue;

		unavailable =
			mh_net_snapshot_stale(MH_NET_WIRE_31_LOBBY,
				host->race.vehicles[i].history.received_ms,
				game_ms) ||
			(host->race.elimination.eliminated & (1U << i));

		if (i != recipient)
			unavailable |= host->config->ghost_all ||
				(host->config->ghost_reverse &&
					host->race.progress[i].wrong_way);

		if (unavailable)
			status->flags |= MH_NET_FLAG_UNAVAILABLE;
	}
}

static enum mh_error_code
heartbeat(
	struct mh_server_host *host, double game_ms, struct mh_error *error_out)
{
	struct mh_net_message message;
	unsigned int i;
	enum mh_error_code status;

	if (game_ms <= ((host->race.heartbeat_ms + HEARTBEAT_MS) & MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mh_net_protocol_has(
		host->protocol, MH_NET_PROTOCOL_APPLICATION_PING)) {
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_PING;

		/*
		 * Echoes use the transport clock, shared with waiting lobby
		 * clients.
		 */
		message.stamp = (unsigned long)fmod(
			mh_transport_now(host->transport), MH_U32_MODULUS);
		status = mh_server_host_multicast(host,
			mh_server_host_members(host, MH_NET_FLAG_RACING),
			&message, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (!(host->lobby.roster.players[i].flags & MH_NET_FLAG_RACING))
			continue;

		race_status(host, i, game_ms, &message);
		status = mh_server_host_send(
			host, host->lobby.peers[i], &message, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	host->race.heartbeat_ms = (unsigned long)game_ms;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
relay_vehicle(struct mh_server_host *host, unsigned int source,
	unsigned int recipient, struct mh_error *error_out)
{
	struct mh_net_message message;
	const struct mh_net_snapshot *snapshot;
	const double *viewed;
	unsigned int view;
	int detailed;
	enum mh_error_code status;

	snapshot = &host->race.vehicles[source].history.entries[0];
	view = host->race.vehicles[recipient].view_player;
	viewed = host->race.vehicles[view].history.entries[0].vehicle.position;
	detailed = mh_net_snapshot_detailed(
		snapshot->vehicle.position, viewed, VIEW_DISTANCE);

	if (detailed < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated relay position");

	status = mh_net_vehicle_message(&snapshot->vehicle, source,
		snapshot->stamp, detailed, 1, &message, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_server_host_send(
		host, host->lobby.peers[recipient], &message, 0, error_out);
}

static enum mh_error_code
relay_updates(
	struct mh_server_host *host, double game_ms, struct mh_error *error_out)
{
	unsigned int recipient, source, flags;
	int due;
	enum mh_error_code status;

	for (recipient = 1; recipient < MH_NET_WIRE_PLAYERS; recipient++) {
		flags = host->lobby.roster.players[recipient].flags;

		if (!(flags & MH_NET_FLAG_RACING) ||
			!(flags & MH_NET_FLAG_READY))
			continue;

		due = mh_net_snapshot_due(host->race.sent_ms[recipient],
			host->lobby.roster.players[recipient].update_rate,
			game_ms);

		if (due < 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid dedicated relay rate");

		if (!due)
			continue;

		for (source = 1; source < MH_NET_WIRE_PLAYERS; source++) {
			if (source == recipient ||
				!(host->lobby.roster.players[source].flags &
					MH_NET_FLAG_RACING))
				continue;

			status = relay_vehicle(
				host, source, recipient, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		host->race.sent_ms[recipient] = (unsigned long)game_ms;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_host_replicate(
	struct mh_server_host *host, struct mh_error *error_out)
{
	double game_ms;
	enum mh_error_code status;

	game_ms = mh_transport_now(host->transport) - host->race.origin_ms;
	status = expire_racers(host, error_out);

	if (status == MH_ERROR_NONE)
		status = heartbeat(host, game_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return relay_updates(host, game_ms, error_out);
}
