#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/simulation_rate.h"
#include "engine/network/join.h"
#include "engine/network/session.h"
#include "engine/network/session_internal.h"
#include "engine/network/snapshot_schedule.h"
#include "engine/ui/lobby_session.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)
/*
 * Static storage keeps the eight complete bounded sessions off the test stack.
 */
static struct mh_net_session host, clients[8], saved;

static struct mh_net_session_config
session_config(unsigned int player)
{
	struct mh_net_session_config settings;

	memset(&settings, 0, sizeof(settings));
	settings.provider = 2;
	strcpy(settings.password, "Motor");
	strcpy(settings.track, "Track1.trk");
	settings.laps = 3;
	settings.car_count = 10;
	strcpy(settings.profile.name, "Driver0");
	settings.profile.name[6] = (char)('0' + player);
	strcpy(settings.profile.car, "Kobra.car");
	strcpy(settings.profile.team, "DICE");
	strcpy(settings.profile.horn, "Horn1.chf");
	settings.profile.update_rate = 15;
	settings.prediction.minimum = MH_NET_PREDICTION_MIN_DEFAULT;
	settings.prediction.maximum = MH_NET_PREDICTION_MAX_DEFAULT;

	return settings;
}

static void
drain(struct mh_net_session *session)
{
	while (mh_net_session_outgoing(session) != NULL)
		mh_net_session_sent(session);
}

/*
 * Protocol-only fixtures have no car clock. Supply the same synthetic ms
 * explicitly to both stages; playable tests use mh_net_race_frame instead.
 */
static enum mh_error_code
service_frame(
	struct mh_net_session *session, double now, struct mh_error *error_out)
{
	struct mh_net_session next;
	enum mh_error_code status;

	next = *session;
	status = mh_net_session_tick(&next, now, error_out);

	if (status == MH_ERROR_NONE &&
		(next.phase == MH_NET_WAITING || next.phase == MH_NET_RACING))
		status = mh_net_session_start_frame(&next, now, error_out);

	if (status == MH_ERROR_NONE)
		*session = next;

	return status;
}

static void
pump(double now)
{
	struct mh_net_outgoing packet;
	const struct mh_net_outgoing *queued;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i, rounds;
	int work;

	for (rounds = 0; rounds < 32; rounds++) {
		work = 0;

		while ((queued = mh_net_session_outgoing(&host)) != NULL) {
			packet = *queued;
			mh_net_session_sent(&host);
			work = 1;
			CHECK(packet.peer >= 2 && packet.peer <= 9);

			if (packet.peer < 2 || packet.peer > 9)
				continue;

			if (packet.size != 0) {
				status = mh_net_session_receive(
					&clients[packet.peer - 2], 1,
					packet.data, packet.size,
					packet.reliable, now, &error);

				if (status != MH_ERROR_NONE)
					fprintf(stderr, "client %lu: %s\n",
						packet.peer, error.message);

				CHECK(status == MH_ERROR_NONE);

				if (status == MH_ERROR_NONE &&
					clients[packet.peer - 2].start_pending)
					CHECK(mh_net_session_start_frame(
						&clients[packet.peer - 2],
						now,
						NULL) == MH_ERROR_NONE);
			}

			if (packet.disconnect) {
				CHECK(mh_net_session_disconnected(
					&clients[packet.peer - 2], 1, now,
					NULL) == MH_ERROR_NONE);
				CHECK(mh_net_session_disconnected(&host,
					packet.peer, now,
					NULL) == MH_ERROR_NONE);
			}
		}

		for (i = 0; i < 8; i++) {
			while ((queued = mh_net_session_outgoing(
					&clients[i])) != NULL) {
				packet = *queued;
				mh_net_session_sent(&clients[i]);
				work = 1;

				if (packet.size != 0) {
					status = mh_net_session_receive(&host,
						i + 2, packet.data, packet.size,
						packet.reliable, now, &error);

					if (status != MH_ERROR_NONE)
						fprintf(stderr,
							"host from %u: %s\n",
							i + 2, error.message);

					CHECK(status == MH_ERROR_NONE);
				}

				if (packet.disconnect) {
					CHECK(mh_net_session_disconnected(&host,
						i + 2, now,
						NULL) == MH_ERROR_NONE);
					CHECK(mh_net_session_disconnected(
						&clients[i], 1, now,
						NULL) == MH_ERROR_NONE);
				}
			}
		}

		if (!work)
			return;
	}

	/* A protocol loop must not spin indefinitely. */
	CHECK(0);
}

static void
setup(void)
{
	struct mh_net_session_config settings;
	unsigned int i;

	settings = session_config(0);
	CHECK(mh_net_session_init(1, &settings, &host, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++) {
		settings = session_config(i + 1);
		CHECK(mh_net_session_init(0, &settings, &clients[i], NULL) ==
			MH_ERROR_NONE);
	}
}

static void
join(unsigned int i, double now)
{
	CHECK(mh_net_session_connected(&host, i + 2, now, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_session_connected(&clients[i], 1, now, NULL) ==
		MH_ERROR_NONE);
	pump(now);
}

/*
 * Isolate subsequent chat assertions from the admission notices. Keep revisions
 * monotonic, just as a presentation consumer does.
 */
static void
clear_chat_windows(void)
{
	unsigned int i;

	host.chat_count = 0;

	for (i = 0; i < 8; i++)
		clients[i].chat_count = 0;
}

static enum mh_error_code
deliver(struct mh_net_session *session, unsigned long peer,
	struct mh_net_message *message, int reliable, double now,
	struct mh_error *error_out)
{
	unsigned char data[MH_NET_PACKET_LIMIT];
	size_t size;
	enum mh_error_code status;

	status = mh_net_encode(message, data, sizeof(data), &size, error_out);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_net_session_receive(
		session, peer, data, size, reliable, now, error_out);
}

static void
lobby_notifications(void)
{
	struct mh_net_message message;
	struct mh_net_batch batch;
	struct mh_net_profile profile;
	unsigned long revision;
	unsigned int i;

	setup();
	CHECK(mh_net_session_connected(&clients[0], 1, 0, NULL) ==
		MH_ERROR_NONE);
	drain(&clients[0]);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	strcpy(message.text, "early");
	saved = clients[0];
	CHECK(deliver(&clients[0], 1, &message, 1, 1, NULL) != MH_ERROR_NONE &&
		!memcmp(&saved, &clients[0], sizeof(saved)));
	message.type = MH_NET_ACCEPT;
	message.player = 1;
	CHECK(deliver(&clients[0], 1, &message, 1, 1, NULL) == MH_ERROR_NONE);
	CHECK(clients[0].chat_count == 1 &&
		!strcmp(clients[0].chat[0].text, "Connection accepted. "));
	CHECK(clients[0].chat[0].color == 0xe06820UL &&
		clients[0].phase == MH_NET_JOINING);
	message.type = MH_NET_CHAT;
	message.player = 0;
	message.color = 0xe06820UL;
	strcpy(message.text, "Driver1 entered the lobby.");
	CHECK(deliver(&clients[0], 1, &message, 1, 2, NULL) == MH_ERROR_NONE &&
		clients[0].chat_count == 2);
	message.player = 1;
	saved = clients[0];
	CHECK(deliver(&clients[0], 1, &message, 1, 3, NULL) != MH_ERROR_NONE &&
		!memcmp(&saved, &clients[0], sizeof(saved)));
	message.player = 0;
	mh_net_batch_reset(&batch);
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE);
	message.type = MH_NET_START;
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_receive(&clients[0], 1, batch.data, batch.size, 1,
		3, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&saved, &clients[0], sizeof(saved)));

	for (i = 0; i < 2; i++) {
		setup();

		if (i)
			CHECK(mh_net_session_begin_race(&host, 0, NULL) ==
				MH_ERROR_NONE);

		CHECK(mh_net_session_connected(&host, 2, 0, NULL) ==
			MH_ERROR_NONE);
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_JOIN;
		message.join.provider = 2;
		message.join.version_bits = MH_NET_ORIGINAL_VERSION_BITS;
		strcpy(message.join.password, "Motor");
		CHECK(deliver(&host, 2, &message, 1, 0, NULL) == MH_ERROR_NONE);
		drain(&host);
		message.type = MH_NET_PROFILE_JOIN;
		message.player = 1;
		message.profile = clients[0].config.profile;
		host.out_count = MH_NET_OUTBOX - 1;
		saved = host;
		CHECK(deliver(&host, 2, &message, 1, 1, NULL) !=
				MH_ERROR_NONE &&
			!memcmp(&saved, &host, sizeof(host)));
		host.out_count = 0;
		CHECK(deliver(&host, 2, &message, 1, 1, NULL) ==
				MH_ERROR_NONE &&
			host.out_count == 2);
		CHECK(host.outgoing[host.out_head].data[0] ==
			(i ? MH_NET_ROSTER : MH_NET_CHAT));
		CHECK(host.outgoing[(host.out_head + 1) % MH_NET_OUTBOX]
				.data[0] == (i ? MH_NET_CHAT : MH_NET_ROSTER));
		CHECK(host.chat_count == (i ? 0U : 1U));
	}

	setup();
	join(0, 0);
	join(1, 1);
	CHECK(host.chat_count == 2 && clients[0].chat_count == 3 &&
		clients[1].chat_count == 2);
	CHECK(!strcmp(host.chat[1].text, "Driver2 entered the lobby.") &&
		host.chat[1].color == 0xe06820UL);
	revision = host.chat_revision;
	profile = clients[0].config.profile;
	strcpy(profile.name, "Renamed");
	CHECK(mh_net_session_profile(&clients[0], &profile, NULL) ==
		MH_ERROR_NONE);
	pump(2);

	/* Profile edits never announce a second join. */
	CHECK(host.chat_revision == revision);
	CHECK(mh_net_session_leave(&clients[0], NULL) == MH_ERROR_NONE);
	pump(3);
	CHECK(!strcmp(host.chat[host.chat_count - 1].text,
		"Renamed left the lobby."));
	CHECK(!strcmp(clients[1].chat[clients[1].chat_count - 1].text,
		"Renamed left the lobby."));
	CHECK(clients[0].phase == MH_NET_CLOSED &&
		clients[1].phase == MH_NET_LOBBY);
	revision = host.chat_revision;
	CHECK(mh_net_session_disconnected(&host, 2, 4, NULL) == MH_ERROR_NONE &&
		host.chat_revision == revision);
	CHECK(mh_net_session_kick(&host, 2, NULL) == MH_ERROR_NONE);
	pump(5);
	CHECK(clients[1].kicked &&
		!strcmp(clients[1].chat[clients[1].chat_count - 1].text,
			"You got kicked by the server!"));
	CHECK(!strcmp(host.chat[host.chat_count - 1].text,
		"Driver2 left the lobby."));

	setup();
	join(0, 0);
	join(1, 0);
	CHECK(mh_net_session_begin_race(&host, 1, NULL) == MH_ERROR_NONE);
	pump(1);
	revision = host.chat_revision;
	join(2, 2);
	CHECK(host.chat_revision == revision && clients[0].chat_revision == 3);
	CHECK(clients[2].chat_count == 2 &&
		!strcmp(clients[2].chat[1].text, "Driver3 entered the lobby."));
	CHECK(mh_net_session_return_lobby(&clients[1], NULL) == MH_ERROR_NONE);
	pump(3);
	join(3, 4);
	CHECK(host.chat_revision == revision && clients[0].chat_revision == 3);
	CHECK(!strcmp(clients[1].chat[clients[1].chat_count - 1].text,
		"Driver4 entered the lobby."));

	setup();
	join(0, 0);
	join(1, 0);
	revision = host.chat_revision;
	host.connections[1].contact_ms = 120000;
	CHECK(service_frame(&host, 120000, NULL) == MH_ERROR_NONE &&
		host.chat_revision == revision);
	drain(&host);
	CHECK(service_frame(&host, 120001, NULL) == MH_ERROR_NONE);
	pump(120001);
	CHECK(!strcmp(
		host.chat[host.chat_count - 1].text, "Driver1 timed out!"));
	CHECK(!strcmp(clients[1].chat[clients[1].chat_count - 1].text,
		"Driver1 timed out!"));

	/* Disconnect acknowledgement adds no duplicate. */
	CHECK(host.chat_revision == revision + 1);

	setup();
	join(0, 0);
	join(1, 0);

	for (i = 0; i < MH_NET_OUTBOX / 2; i++)
		CHECK(mh_net_session_chat(&host, "fill", 0, NULL) ==
			MH_ERROR_NONE);

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_LOBBY;
	message.player = 1;
	saved = host;
	CHECK(deliver(&host, 2, &message, 1, 1, NULL) != MH_ERROR_NONE &&
		!memcmp(&saved, &host, sizeof(host)));
}

static void
full_lobby(void)
{
	struct mh_net_profile profile;
	unsigned int i, j;

	setup();

	for (i = 0; i < 7; i++)
		join(i, i * 10);

	for (i = 0; i < 7; i++) {
		CHECK(clients[i].phase == MH_NET_LOBBY &&
			clients[i].player == i + 1);
		CHECK(memcmp(&clients[i].roster, &host.roster,
			sizeof(host.roster)) == 0);

		for (j = 0; j < 8; j++)
			CHECK(clients[i].roster.players[j].flags == 2);
	}

	join(7, 80);
	CHECK(clients[7].phase == MH_NET_REJECTED &&
		clients[7].rejection_reason == 1);
	CHECK(strcmp(clients[7].rejection, "Server full!") == 0);
	profile = clients[2].config.profile;
	strcpy(profile.name, "Renamed");
	CHECK(mh_net_session_profile(&clients[2], &profile, NULL) ==
		MH_ERROR_NONE);
	pump(90);

	for (i = 0; i < 7; i++)
		CHECK(strcmp(clients[i].roster.players[3].name, "Renamed") ==
			0);

	clear_chat_windows();
	CHECK(mh_net_session_chat(&clients[3], "hello", 0x123456UL, NULL) ==
		MH_ERROR_NONE);
	pump(100);
	CHECK(host.chat_count == 1 && host.chat[0].player == 4);

	for (i = 0; i < 7; i++)
		CHECK(clients[i].chat_count == 1 &&
			strcmp(clients[i].chat[0].text,
				i == 3 ? "hello" : "Driver4: hello") == 0);

	CHECK(mh_net_session_chat(&host, "welcome", 3, NULL) == MH_ERROR_NONE);
	pump(110);

	for (i = 0; i < 7; i++)
		CHECK(clients[i].chat_count == 2 &&
			clients[i].chat[1].player == 0);

	CHECK(service_frame(&host, 1101, NULL) == MH_ERROR_NONE);
	pump(1113);

	for (i = 0; i < 7; i++)
		CHECK(!host.connections[i].ping_pending &&
			host.connections[i].ping_ms == 12);

	CHECK(mh_net_session_leave(&clients[2], NULL) == MH_ERROR_NONE);
	pump(1200);
	CHECK(host.roster.players[3].flags == 0 &&
		clients[2].phase == MH_NET_CLOSED);

	for (i = 0; i < 7; i++)
		if (i != 2)
			CHECK(clients[i].roster.players[3].flags == 0);

	CHECK(mh_net_session_leave(&host, NULL) == MH_ERROR_NONE);
	pump(1300);

	for (i = 0; i < 7; i++)
		CHECK(clients[i].phase == MH_NET_CLOSED);

	CHECK(host.out_count == 0);
}

static void
invalid_and_backpressure(void)
{
	struct mh_net_message message;
	struct mh_net_batch batch;
	struct mh_net_session_config settings;
	unsigned int i;

	setup();
	join(0, 0);
	join(1, 10);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	message.player = 2;
	strcpy(message.text, "spoof");
	saved = host;
	CHECK(deliver(&host, 2, &message, 1, 20, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);
	message.player = 1;
	CHECK(deliver(&host, 2, &message, 0, 20, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);
	CHECK(deliver(&host, 900, &message, 1, 20, NULL) != MH_ERROR_NONE);
	mh_net_batch_reset(&batch);
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE);
	message.type = MH_NET_ACCEPT;
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_receive(&host, 2, batch.data, batch.size, 1, 20,
		NULL) != MH_ERROR_NONE);

	/* Valid first frame also rolled back. */
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);
	batch.data[batch.size - 3] = 0xff;
	CHECK(mh_net_session_receive(&host, 2, batch.data, batch.size, 1, 20,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);

	for (i = 0; i < MH_NET_OUTBOX / 2; i++)
		CHECK(mh_net_session_chat(&host, "queued", i, NULL) ==
			MH_ERROR_NONE);

	CHECK(host.out_count == MH_NET_OUTBOX);
	saved = host;
	CHECK(mh_net_session_chat(&host, "must roll back", 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);
	drain(&host);
	CHECK(service_frame(&host, 9, NULL) != MH_ERROR_NONE);
	settings = session_config(1);
	settings.profile.update_rate = 0;
	CHECK(mh_net_session_profile(&clients[0], &settings.profile, NULL) !=
		MH_ERROR_NONE);
	message.type = MH_NET_PROFILE_UPDATE;
	message.player = 2;
	message.profile = clients[0].config.profile;
	saved = host;
	CHECK(deliver(&host, 2, &message, 1, 20, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);
}

static void
phases_and_timeouts(void)
{
	struct mh_net_session_config settings;
	struct mh_net_message message;
	struct mh_net_batch batch;

	setup();
	strcpy(clients[0].config.password, "wrong");
	join(0, 0);
	CHECK(clients[0].phase == MH_NET_REJECTED &&
		clients[0].rejection_reason == 2);
	CHECK(host.roster.players[1].flags == 0);
	settings = session_config(1);
	strcpy(settings.password, "mOTOR");
	CHECK(mh_net_session_init(0, &settings, &clients[0], NULL) ==
		MH_ERROR_NONE);
	join(0, 10);
	CHECK(clients[0].phase == MH_NET_LOBBY);
	CHECK(mh_net_session_connected(&host, 9, 20, NULL) == MH_ERROR_NONE);
	CHECK(service_frame(&host, 5020, NULL) == MH_ERROR_NONE);
	CHECK(!host.connections[1].closing);
	drain(&host);
	CHECK(service_frame(&host, 5021, NULL) == MH_ERROR_NONE &&
		host.connections[1].closing);
	drain(&host);
	CHECK(service_frame(&host, 120010, NULL) == MH_ERROR_NONE &&
		host.roster.players[1].flags == 2);
	drain(&host);
	CHECK(service_frame(&host, 120011, NULL) == MH_ERROR_NONE &&
		host.roster.players[1].flags == 0);
	drain(&host);
	CHECK(service_frame(&clients[0], 120010, NULL) == MH_ERROR_NONE &&
		clients[0].phase == MH_NET_LOBBY);
	CHECK(service_frame(&clients[0], 120011, NULL) == MH_ERROR_NONE &&
		clients[0].phase == MH_NET_CLOSED);
	CHECK(!strcmp(clients[0].chat[clients[0].chat_count - 1].text,
		"Server connection timed out!"));
	setup();
	CHECK(mh_net_session_connected(&clients[0], 1, 0, NULL) ==
		MH_ERROR_NONE);
	drain(&clients[0]);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PING;
	CHECK(deliver(&clients[0], 1, &message, 0, 1, NULL) == MH_ERROR_NONE &&
		clients[0].out_count == 0);
	message.type = MH_NET_REJECT;
	message.reason = 1;
	strcpy(message.text, "closed");
	mh_net_batch_reset(&batch);
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE);
	message.type = MH_NET_ACCEPT;
	message.player = 1;
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE);
	saved = clients[0];
	CHECK(mh_net_session_receive(&clients[0], 1, batch.data, batch.size, 1,
		2, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);
	message.type = MH_NET_ACCEPT;
	CHECK(deliver(&clients[0], 1, &message, 1, 2, NULL) == MH_ERROR_NONE);
	saved = clients[0];
	CHECK(deliver(&clients[0], 1, &message, 1, 2, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);
}

static void
connection_rollback(void)
{
	const struct mh_net_outgoing *outgoing;
	unsigned int i;

	setup();
	join(0, 0);
	join(1, 0);

	/* A queued ping must roll back if the following timeout cannot fit. */
	for (i = 0; i < MH_NET_OUTBOX / 2; i++)
		CHECK(mh_net_session_chat(&host, "queued", i, NULL) ==
			MH_ERROR_NONE);

	mh_net_session_sent(&host);
	host.connections[0].contact_ms = 120001;
	saved = host;
	CHECK(mh_net_session_tick(&host, 120001, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);

	drain(&host);
	CHECK(mh_net_session_tick(&host, 120001, NULL) == MH_ERROR_NONE);
	CHECK(host.connections[0].ping_pending);
	CHECK(!host.connections[0].closing && host.connections[1].closing);
	outgoing = mh_net_session_outgoing(&host);
	CHECK(outgoing != NULL && outgoing->peer == 2 && !outgoing->reliable &&
		!outgoing->disconnect && outgoing->data[0] == MH_NET_PING);
	mh_net_session_sent(&host);
	outgoing = mh_net_session_outgoing(&host);
	CHECK(outgoing != NULL && outgoing->peer == 3 && outgoing->reliable &&
		outgoing->disconnect && outgoing->size == 0);

	/* Client timeout text and phase are also part of the transaction. */
	for (i = 0; i < MH_NET_OUTBOX; i++)
		CHECK(mh_net_session_chat(&clients[0], "queued", i, NULL) ==
			MH_ERROR_NONE);

	saved = clients[0];
	CHECK(mh_net_session_tick(&clients[0], 120001, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);
}

static void
disconnect_output_order(void)
{
	struct mh_net_outgoing first, second;

	setup();
	join(0, 0);
	join(1, 0);

	/* Cross the ring boundary with alternating recipients. */
	host.out_head = MH_NET_OUTBOX - 2;
	CHECK(mh_net_session_chat(&host, "first", 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_chat(&host, "second", 2, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 4);
	first = host.outgoing[MH_NET_OUTBOX - 1];
	second = host.outgoing[1];
	CHECK(first.peer == 3 && second.peer == 3);
	CHECK(mh_net_session_disconnected(&host, 2, 1, NULL) == MH_ERROR_NONE);
	CHECK(host.connections[0].peer == 0);
	CHECK(host.roster.players[1].flags == 0);
	CHECK(host.out_head == 0 && host.out_count == 4);
	CHECK(!memcmp(&host.outgoing[0], &first, sizeof(first)));
	CHECK(!memcmp(&host.outgoing[1], &second, sizeof(second)));
	CHECK(host.outgoing[2].data[0] == MH_NET_CHAT);
	CHECK(host.outgoing[3].data[0] == MH_NET_ROSTER);
}

static void
race_start(void)
{
	struct mh_net_message message;
	unsigned int i, mask;
	double anchor;

	setup();

	for (i = 0; i < 7; i++)
		join(i, i * 10);

	CHECK(mh_net_session_begin_race(&clients[0], 100, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(&host, 100, NULL) != MH_ERROR_NONE);
	CHECK(mh_net_session_begin_race(&host, 100, NULL) == MH_ERROR_NONE);
	pump(100);
	CHECK(host.phase == MH_NET_LOADING && host.ready_mask == 0);
	mask = 0;

	for (i = 0; i < 8; i++) {
		CHECK(host.roster.players[i].value_a >= 1 &&
			host.roster.players[i].value_a <= 8);
		mask |= 1U << host.roster.players[i].value_a;
	}

	CHECK(mask == 510);

	for (i = 0; i < 7; i++) {
		CHECK(clients[i].phase == MH_NET_LOADING);
		CHECK(memcmp(&clients[i].roster, &host.roster,
			sizeof(host.roster)) == 0);
		CHECK(clients[i].roster.players[0].flags == 1 &&
			clients[i].roster.players[i + 1].flags == 1);
		CHECK(mh_net_session_profile(&clients[i],
			&clients[i].config.profile,
			NULL) != MH_ERROR_NONE);
	}

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_START;
	saved = clients[0];
	CHECK(deliver(&clients[0], 1, &message, 1, 100, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&clients[0], &saved, sizeof(saved)) == 0);
	CHECK(mh_net_session_loaded(&host, 200, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 6; i++)
		CHECK(mh_net_session_loaded(&clients[i], 200, NULL) ==
			MH_ERROR_NONE);

	CHECK(service_frame(&host, 1001, NULL) == MH_ERROR_NONE);
	pump(1021);

	/* The slow loader replies with lobby pong. */
	CHECK(host.ready_mask == 127);
	CHECK(service_frame(&host, 2000, NULL) == MH_ERROR_NONE &&
		host.start.anchor_ms == 0);
	CHECK(mh_net_session_loaded(&clients[6], 2000, NULL) == MH_ERROR_NONE);
	CHECK(service_frame(&host, 2002, NULL) == MH_ERROR_NONE);
	pump(2042);
	CHECK(host.ready_mask == 255);
	CHECK(service_frame(&host, 2042, NULL) == MH_ERROR_NONE);
	pump(2042);
	anchor = host.start.anchor_ms;
	CHECK(anchor == 2042);
	clear_chat_windows();
	CHECK(mh_net_session_chat(&clients[0], "ready", 7, NULL) ==
		MH_ERROR_NONE);
	pump(2043);
	CHECK(host.chat_count == 1);
	CHECK(service_frame(&host, 3040, NULL) == MH_ERROR_NONE);
	pump(3080);
	CHECK(service_frame(&host, 4022, NULL) == MH_ERROR_NONE);
	pump(4022);
	CHECK(host.start.sent_mask == 0); /* Strict > anchor+2000-half RTT. */
	CHECK(service_frame(&host, 4023, NULL) == MH_ERROR_NONE);
	pump(4043);

	for (i = 0; i < 7; i++)
		CHECK(clients[i].phase == MH_NET_RACING &&
			clients[i].start.started_ms == 4043);

	CHECK(host.phase == MH_NET_WAITING);
	CHECK(service_frame(&host, 4043, NULL) == MH_ERROR_NONE &&
		host.phase == MH_NET_RACING);
	CHECK(host.start.started_ms == 4043 && host.start.sent_mask == 254);
	pump(4043);
	CHECK(mh_net_session_leave(&clients[2], NULL) == MH_ERROR_NONE);
	pump(4050);
	CHECK(host.roster.players[3].flags == 0 &&
		clients[2].phase == MH_NET_CLOSED);

	for (i = 0; i < 7; i++)
		if (i != 2)
			CHECK(clients[i].roster.players[3].flags == 0);

	CHECK(mh_net_session_return_lobby(&clients[0], NULL) == MH_ERROR_NONE);
	pump(4080);
	CHECK(clients[0].phase == MH_NET_LOBBY && host.phase == MH_NET_RACING);
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE);
	pump(4090);
	CHECK(host.phase == MH_NET_LOBBY && host.start.anchor_ms == 0);

	for (i = 0; i < 7; i++)
		if (i != 2) {
			CHECK(clients[i].phase == MH_NET_LOBBY);
			CHECK(memcmp(&clients[i].roster, &host.roster,
				sizeof(host.roster)) == 0);
		}

	CHECK(host.return_pending == (254U & ~((1U << 1) | (1U << 3))));
	saved = host;
	CHECK(mh_net_session_begin_race(&host, 4091, NULL) != MH_ERROR_NONE &&
		!memcmp(&host, &saved, sizeof(host)));
	CHECK(service_frame(&host, 4090, NULL) == MH_ERROR_NONE);
	pump(4090);
	CHECK(host.return_pending == 0);

	/* A second race resets the barrier and every readiness bit. */
	CHECK(mh_net_session_begin_race(&host, 4091, NULL) == MH_ERROR_NONE);
	pump(4091);
	CHECK(host.ready_mask == 0 && host.start.started_ms == 0);
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE);
	pump(4092);
	message.type = MH_NET_PONG;
	message.player = 1;
	message.stamp = 4043;
	CHECK(deliver(&host, 2, &message, 0, 4093, NULL) == MH_ERROR_NONE &&
		host.ready_mask == 0);
	CHECK(mh_net_session_leave(&host, NULL) == MH_ERROR_NONE);
	pump(4100);

	for (i = 0; i < 7; i++)
		CHECK(clients[i].phase == MH_NET_CLOSED);
}

static void
race_start_clocks(void)
{
	struct mh_net_message message;
	unsigned int i;

	setup();
	join(0, 0);
	join(1, 1);
	CHECK(mh_net_session_begin_race(&host, 100, NULL) == MH_ERROR_NONE);
	pump(100);
	CHECK(mh_net_session_loaded(&host, 200, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 2; i++)
		CHECK(mh_net_session_loaded(&clients[i], 200, NULL) ==
			MH_ERROR_NONE);

	CHECK(mh_net_session_tick(&host, 1001, NULL) == MH_ERROR_NONE);
	pump(1021);
	CHECK(host.ready_mask == 7 && host.start.anchor_ms == 0);
	CHECK(mh_net_session_tick(&host, 5000, NULL) == MH_ERROR_NONE);
	drain(&host);
	CHECK(host.start.anchor_ms == 0 && host.phase == MH_NET_WAITING);

	for (i = 0; i < MH_NET_CONNECTIONS; i++) {
		if (host.connections[i].peer == 2)
			host.connections[i].ping_ms = 100;

		if (host.connections[i].peer == 3)
			host.connections[i].ping_ms = 4001;
	}

	CHECK(mh_net_session_start_frame(&host,
		(long double)(double)100.002 * 1000.0L,
		NULL) == MH_ERROR_NONE);
	CHECK(host.start.anchor_ms == 100001 && host.now_ms == 5000 &&
		host.out_count == 0);
	CHECK(mh_net_session_start_frame(&host,
		(long double)(double)101.001 * 1000.0L,
		NULL) == MH_ERROR_NONE);
	CHECK(host.start.sent_mask == 4 && host.phase == MH_NET_WAITING &&
		host.out_count == 1);
	CHECK(host.outgoing[host.out_head].peer == 3 &&
		host.outgoing[host.out_head].reliable);
	drain(&host);

	for (i = 0; i < MH_NET_OUTBOX / 2; i++)
		CHECK(mh_net_session_chat(&host, "fill", 0, NULL) ==
			MH_ERROR_NONE);

	saved = host;
	CHECK(mh_net_session_start_frame(&host, 101952, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&host, &saved, sizeof(host)));
	drain(&host);
	CHECK(mh_net_session_start_frame(&host, 101952, NULL) ==
			MH_ERROR_NONE &&
		host.start.sent_mask == 6);
	CHECK(mh_net_session_start_frame(&host, 102001, NULL) ==
			MH_ERROR_NONE &&
		host.phase == MH_NET_WAITING);
	CHECK(mh_net_session_start_frame(&host,
		(long double)(double)102.001 * 1000.0L,
		NULL) == MH_ERROR_NONE);
	CHECK(host.phase == MH_NET_RACING && host.start.started_ms == 102001 &&
		host.now_ms == 5000);
	saved = host;
	CHECK(mh_net_session_start_frame(&host, 102001, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&host, &saved, sizeof(host)));
	CHECK(mh_net_session_start_frame(&host, MH_U32_MODULUS, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&host, &saved, sizeof(host)));

	/*
	 * The original keeps visiting unsent slots after its own release. A
	 * wrapped half-RTT deadline can be much later than that local start.
	 */
	drain(&host);
	mh_net_start_reset(&host.start);
	host.phase = MH_NET_WAITING;

	for (i = 0; i < MH_NET_CONNECTIONS; i++) {
		if (host.connections[i].peer == 2)
			host.connections[i].ping_ms = 0;

		if (host.connections[i].peer == 3)
			host.connections[i].ping_ms = MH_U32_MAX;
	}

	CHECK(mh_net_session_start_frame(&host, 1000, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_start_frame(&host, 3001, NULL) == MH_ERROR_NONE &&
		host.phase == MH_NET_RACING && host.start.sent_mask == 2);
	CHECK(mh_net_session_start_frame(&host, 2147486650.0L, NULL) ==
			MH_ERROR_NONE &&
		host.start.sent_mask == 6);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_START;
	CHECK(deliver(&clients[0], 1, &message, 1, 60000, NULL) ==
		MH_ERROR_NONE);
	CHECK(clients[0].phase == MH_NET_WAITING && clients[0].start_pending &&
		clients[0].start.started_ms == 0);
	CHECK(mh_net_session_tick(&clients[0], 61000, NULL) == MH_ERROR_NONE &&
		clients[0].phase == MH_NET_WAITING);
	CHECK(mh_net_session_start_frame(&clients[0],
		(long double)(double)100.002 * 1000.0L,
		NULL) == MH_ERROR_NONE);
	CHECK(clients[0].phase == MH_NET_RACING && !clients[0].start_pending &&
		clients[0].start.started_ms == 100001 &&
		clients[0].now_ms == 61000);
	CHECK(mh_net_session_return_lobby(&clients[0], NULL) == MH_ERROR_NONE &&
		!clients[0].start_pending);
	saved = clients[0];
	saved.contact_ms = 61000;
	CHECK(deliver(&clients[0], 1, &message, 1, 61000, NULL) ==
		MH_ERROR_NONE);
	CHECK(!memcmp(&clients[0], &saved, sizeof(saved)));
	CHECK(deliver(&clients[1], 1, &message, 1, 60000, NULL) ==
			MH_ERROR_NONE &&
		clients[1].start_pending);
	CHECK(mh_net_session_return_lobby(&clients[1], NULL) == MH_ERROR_NONE &&
		!clients[1].start_pending);
	saved = clients[1];
	CHECK(deliver(&clients[1], 1, &message, 1, 60000, NULL) ==
		MH_ERROR_NONE);
	CHECK(!memcmp(&clients[1], &saved, sizeof(saved)));
	CHECK(mh_net_session_start_frame(&clients[1], 100000, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&clients[1], &saved, sizeof(saved)));
}

static void
return_before_start_receipt(void)
{
	unsigned int i;

	setup();
	join(0, 0);
	CHECK(mh_net_session_begin_race(&host, 1, NULL) == MH_ERROR_NONE);
	pump(1);
	CHECK(mh_net_session_loaded(&host, 2, NULL) == MH_ERROR_NONE &&
		mh_net_session_loaded(&clients[0], 2, NULL) == MH_ERROR_NONE);
	CHECK(service_frame(&host, 1001, NULL) == MH_ERROR_NONE);
	pump(1001);
	CHECK(host.ready_mask == 3);
	CHECK(service_frame(&host, 1001, NULL) == MH_ERROR_NONE &&
		host.start.anchor_ms == 1001);
	CHECK(service_frame(&host, 3002, NULL) == MH_ERROR_NONE &&
		host.start.sent_mask == 2);

	/*
	 * The release is queued at the host while Home changes the client's
	 * phase before its next network drain, as in the real X11 failure.
	 */
	CHECK(mh_net_session_return_lobby(&clients[0], NULL) == MH_ERROR_NONE);
	pump(3002);
	CHECK(clients[0].phase == MH_NET_LOBBY &&
		clients[0].start.started_ms == 0 && !clients[0].start_pending);
	CHECK(host.phase == MH_NET_RACING &&
		host.lobby.slots[1].phase == MH_NET_IN_LOBBY);
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE);
	pump(3003);
	CHECK(host.return_pending == 0 && clients[0].phase == MH_NET_LOBBY);

	for (i = 0; i < 2; i++) {
		CHECK(mh_net_session_begin_race(&host, 3010 + i * 4000, NULL) ==
			MH_ERROR_NONE);
		pump(3010 + i * 4000);
		CHECK(clients[0].phase == MH_NET_LOADING &&
			!clients[0].start_pending);
		CHECK(mh_net_session_return_lobby(&clients[0], NULL) ==
			MH_ERROR_NONE);
		pump(3011 + i * 4000);
		CHECK(mh_net_session_return_lobby(&host, NULL) ==
			MH_ERROR_NONE);
		pump(3012 + i * 4000);
		CHECK(clients[0].phase == MH_NET_LOBBY &&
			host.return_pending == 0);
	}
}

static void
race_invalid_and_disconnect(void)
{
	struct mh_net_message message;
	struct mh_net_session_config settings;
	unsigned int i;

	setup();
	join(0, 0);
	join(1, 1);
	CHECK(mh_net_session_connected(&host, 9, 2, NULL) == MH_ERROR_NONE);
	saved = host;
	CHECK(mh_net_session_begin_race(&host, 3, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);
	CHECK(mh_net_session_disconnected(&host, 9, 3, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_NET_OUTBOX / 2; i++)
		CHECK(mh_net_session_chat(&host, "fill", i, NULL) ==
			MH_ERROR_NONE);

	saved = host;
	CHECK(mh_net_session_begin_race(&host, 4, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);
	drain(&host);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_RACE_CONFIG;
	message.roster = host.roster;
	saved = clients[0];
	CHECK(deliver(&clients[0], 1, &message, 0, 4, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);
	CHECK(deliver(&clients[0], 1, &message, 1, 4, NULL) !=
		MH_ERROR_NONE); /* Duplicate/unassigned grid ranks. */
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);
	CHECK(deliver(&host, 2, &message, 1, 4, NULL) != MH_ERROR_NONE);
	CHECK(mh_net_session_begin_race(&host, 10, NULL) == MH_ERROR_NONE);
	pump(10);
	CHECK(mh_net_session_connected(&host, 9, 11, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_disconnected(&host, 9, 11, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(&host, 11, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(&clients[0], 11, NULL) == MH_ERROR_NONE);
	CHECK(service_frame(&host, 1001, NULL) == MH_ERROR_NONE);
	drain(&host);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PONG;
	message.player = 1;
	message.stamp = 1000;
	CHECK(deliver(&host, 2, &message, 0, 1002, NULL) == MH_ERROR_NONE &&
		host.ready_mask == 1); /* unmatched */
	message.stamp = 1001;
	saved = host;
	CHECK(deliver(&host, 3, &message, 0, 1003, NULL) != MH_ERROR_NONE &&
		memcmp(&host, &saved, sizeof(host)) == 0);
	CHECK(deliver(&host, 2, &message, 0, 1003, NULL) == MH_ERROR_NONE &&
		host.ready_mask == 3);
	CHECK(service_frame(&host, 1003, NULL) == MH_ERROR_NONE &&
		host.start.anchor_ms == 0);
	CHECK(mh_net_session_disconnected(&host, 3, 1004, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_session_disconnected(&clients[1], 1, 1004, NULL) ==
		MH_ERROR_NONE);
	pump(1004);
	CHECK(clients[0].roster.players[2].flags == 0);
	CHECK(service_frame(&host, 1004, NULL) == MH_ERROR_NONE &&
		host.start.anchor_ms == 1004);

	for (i = 0; i < MH_NET_OUTBOX; i++)
		CHECK(mh_net_session_chat(&host, "fill", i, NULL) ==
			MH_ERROR_NONE);

	saved = host;
	CHECK(service_frame(&host, 4000, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);
	drain(&host);
	CHECK(service_frame(&host, 4000, NULL) == MH_ERROR_NONE &&
		host.phase == MH_NET_RACING);
	pump(4001);
	settings = session_config(2);
	CHECK(mh_net_session_init(0, &settings, &clients[1], NULL) ==
		MH_ERROR_NONE);
	CHECK(service_frame(&host, 124002, NULL) == MH_ERROR_NONE);
	drain(&host);
	CHECK(host.roster.players[1].flags == 0);
	CHECK(service_frame(&clients[0], 124002, NULL) == MH_ERROR_NONE &&
		clients[0].phase == MH_NET_CLOSED);
}

static void
mixed_participation(void)
{
	struct mh_net_profile profile;
	struct mh_net_message message;
	struct mh_net_session_config settings;
	unsigned int i, positions[MH_NET_PLAYERS];

	setup();
	join(0, 0);
	join(1, 1);
	CHECK(mh_net_session_begin_race(&host, 10, NULL) == MH_ERROR_NONE);
	pump(10);
	join(2, 11); /* New lobby member while the original field is loading. */
	CHECK(host.phase == MH_NET_LOADING && clients[2].phase == MH_NET_LOBBY);
	CHECK(host.roster.players[3].flags == 2 &&
		clients[2].roster.players[0].flags == 1);
	CHECK(clients[0].roster.players[3].flags == 0 &&
		clients[1].roster.players[3].flags == 0);
	profile = clients[2].config.profile;
	strcpy(profile.name, "NextRace");
	strcpy(profile.horn, "Different");
	profile.flags = 1;
	profile.values[0] = 73;
	CHECK(mh_net_session_profile(&clients[2], &profile, NULL) ==
		MH_ERROR_NONE);
	pump(12);
	CHECK(host.roster.players[3].flags == 2 &&
		host.roster.players[3].values[0] == 73);
	CHECK(strcmp(host.roster.players[3].name, "NextRace") == 0);
	CHECK(strcmp(host.roster.players[3].horn, "Horn1.chf") == 0);
	CHECK(strcmp(clients[2].roster.players[3].horn, "Horn1.chf") == 0);
	CHECK(mh_net_session_loaded(&host, 20, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(&clients[0], 20, NULL) == MH_ERROR_NONE);
	CHECK(service_frame(&host, 1001, NULL) == MH_ERROR_NONE);
	pump(1001);
	CHECK(host.ready_mask == 3 && host.start.anchor_ms == 0);
	CHECK(mh_net_session_loaded(&clients[1], 1100, NULL) == MH_ERROR_NONE);
	CHECK(service_frame(&host, 2002, NULL) == MH_ERROR_NONE);
	pump(2002);
	CHECK(host.ready_mask == 7);
	CHECK(service_frame(&host, 2003, NULL) == MH_ERROR_NONE);
	pump(2003);
	CHECK(service_frame(&host, 4004, NULL) == MH_ERROR_NONE);
	pump(4004);
	CHECK(host.phase == MH_NET_RACING &&
		clients[0].phase == MH_NET_RACING &&
		clients[1].phase == MH_NET_RACING &&
		clients[2].phase == MH_NET_LOBBY);
	join(3, 4005);
	CHECK(clients[3].phase == MH_NET_LOBBY &&
		host.roster.players[4].flags == 2);

	for (i = 0; i < MH_NET_OUTBOX; i++)
		CHECK(mh_net_session_chat(&clients[0], "queued", 7, NULL) ==
			MH_ERROR_NONE);

	saved = clients[0];
	CHECK(mh_net_session_return_lobby(&clients[0], NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);
	drain(&clients[0]);
	CHECK(mh_net_session_return_lobby(&clients[0], NULL) == MH_ERROR_NONE);
	pump(4010);
	CHECK(host.phase == MH_NET_RACING && clients[1].phase == MH_NET_RACING);
	CHECK(clients[0].phase == MH_NET_LOBBY &&
		host.lobby.slots[1].phase == MH_NET_IN_LOBBY);
	CHECK(host.roster.players[1].flags == 2 &&
		clients[1].roster.players[1].flags == 2);
	CHECK(!(host.ready_mask & 2) && host.replication.history[1].count == 0);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CAR;
	message.player = 1;
	message.view_player = 0;
	message.stamp = 4011;
	CHECK(deliver(&host, 2, &message, 0, 4011, NULL) == MH_ERROR_NONE &&
		host.replication.history[1].count == 0);
	clear_chat_windows();
	CHECK(mh_net_session_chat(&clients[0], "lobby only", 7, NULL) ==
		MH_ERROR_NONE);
	pump(4012);
	CHECK(host.chat_count == 0 && clients[1].chat_count == 0);
	CHECK(clients[2].chat_count == 1 && clients[3].chat_count == 1);
	CHECK(mh_net_session_chat(&clients[1], "race only", 8, NULL) ==
		MH_ERROR_NONE);
	pump(4013);
	CHECK(host.chat_count == 1 && clients[1].chat_count == 1);
	CHECK(clients[2].chat_count == 1 && clients[3].chat_count == 1);
	CHECK(mh_net_session_chat(&host, "host race", 9, NULL) ==
		MH_ERROR_NONE);
	pump(4014);
	CHECK(clients[1].chat_count == 2 && clients[2].chat_count == 1);

	/*
	 * Cross an individual return with the host's completion. The host has
	 * not received the client's request yet, so that racer still scores.
	 */
	CHECK(mh_net_session_return_lobby(&clients[1], NULL) == MH_ERROR_NONE);
	memset(positions, 0, sizeof(positions));
	positions[0] = 1;
	positions[2] = 2;
	CHECK(mh_net_session_finish_race(&host, positions, NULL) ==
		MH_ERROR_NONE);
	pump(4020);
	CHECK(host.roster.players[0].values[4] == 1 &&
		host.roster.players[2].values[4] == 1);
	CHECK(host.roster.players[1].values[4] == 0 &&
		host.roster.players[3].values[4] == 0 &&
		host.roster.players[4].values[4] == 0);

	for (i = 0; i < 4; i++) {
		CHECK(clients[i].phase == MH_NET_LOBBY);
		CHECK(memcmp(&clients[i].roster, &host.roster,
			sizeof(host.roster)) == 0);
	}

	CHECK(host.return_pending == (1U << 2));
	CHECK(service_frame(&host, 4021, NULL) == MH_ERROR_NONE);
	pump(4021);
	CHECK(host.return_pending == 0);
	CHECK(mh_net_session_begin_race(&host, 4030, NULL) == MH_ERROR_NONE);
	pump(4030);

	for (i = 0; i < 4; i++)
		CHECK(clients[i].phase == MH_NET_LOADING);

	CHECK(mh_net_session_leave(&clients[3], NULL) == MH_ERROR_NONE);
	pump(4031);
	CHECK(host.roster.players[4].flags == 0 &&
		clients[3].phase == MH_NET_CLOSED);
	settings = session_config(4);
	CHECK(mh_net_session_init(0, &settings, &clients[3], NULL) ==
		MH_ERROR_NONE);
	join(3, 4032);
	CHECK(clients[3].player == 4 && clients[3].phase == MH_NET_LOBBY);

	/* Reused lobby slot does not respawn a racer. */
	CHECK(clients[0].roster.players[4].flags == 0);
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE);
	pump(4033);

	for (i = 0; i < 4; i++)
		CHECK(memcmp(&clients[i].roster, &host.roster,
			sizeof(host.roster)) == 0);
}

static void
kicking(void)
{
	unsigned int i;

	setup();
	join(0, 0);
	join(1, 1);
	CHECK(mh_net_session_kick(&clients[0], 2, NULL) != MH_ERROR_NONE);
	CHECK(mh_net_session_kick(&host, 0, NULL) != MH_ERROR_NONE &&
		mh_net_session_kick(&host, 7, NULL) != MH_ERROR_NONE);

	for (i = 0; i < MH_NET_OUTBOX / 2; i++)
		CHECK(mh_net_session_chat(&host, "fill", i, NULL) ==
			MH_ERROR_NONE);

	saved = host;
	CHECK(mh_net_session_kick(&host, 1, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &host, sizeof(host)) == 0);
	drain(&host);
	CHECK(mh_net_session_kick(&host, 1, NULL) == MH_ERROR_NONE);
	pump(2);
	CHECK(clients[0].phase == MH_NET_CLOSED && clients[0].kicked);
	CHECK(host.roster.players[1].flags == 0 &&
		clients[1].roster.players[1].flags == 0);
	CHECK(clients[1].phase == MH_NET_LOBBY);
}

static struct mh_net_vehicle
vehicle_snapshot(unsigned int player)
{
	struct mh_net_vehicle vehicle;
	unsigned int i;

	memset(&vehicle, 0, sizeof(vehicle));

	for (i = 0; i < 4; i++)
		vehicle.extension[i] = .8;

	vehicle.rpm = 5000;
	vehicle.gear = 3;
	vehicle.flags = 1;
	vehicle.position[0] = player * 100;

	return vehicle;
}

static void
configure_replication(struct mh_net_session *session)
{
	double position[MH_NET_PLAYERS][3];
	struct mh_net_vehicle vehicle;
	unsigned int i;

	memset(position, 0, sizeof(position));

	for (i = 0; i < MH_NET_PLAYERS; i++)
		position[i][0] = i * 100;

	CHECK(mh_net_session_configure_replication(session, 180,
		(const double (*)[3])position, NULL) == MH_ERROR_NONE);
	vehicle = vehicle_snapshot(session->player);
	CHECK(mh_net_session_publish(session, &vehicle, session->player,
		NULL) == MH_ERROR_NONE);
}

static unsigned int
snapshot_type(unsigned int recipient, unsigned int source)
{
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_message message;
	const struct mh_net_outgoing *out;
	unsigned int i;

	for (i = 0; i < host.out_count; i++) {
		out = &host.outgoing[(host.out_head + i) % MH_NET_OUTBOX];

		if (out->peer != recipient + 1)
			continue;

		CHECK(mh_net_begin(out->data, out->size, &reader, NULL) ==
			MH_ERROR_NONE);

		while (mh_net_next(&reader, &frame) == 1) {
			CHECK(mh_net_decode(&frame, &message, NULL) ==
				MH_ERROR_NONE);

			if ((message.type == MH_NET_CAR ||
				message.type == MH_NET_CAR_FAR) &&
				message.player == source) {
				CHECK(!out->reliable && !out->disconnect);
				return message.type;
			}
		}
	}

	return 0;
}

static void
replication(void)
{
	struct mh_net_session *session;
	struct mh_net_vehicle vehicle;
	struct mh_net_playback playback;
	struct mh_net_message message;
	struct mh_net_batch batch;
	unsigned int i, j, tick;
	double received;
	unsigned long stamp;

	setup();

	for (i = 0; i < 7; i++)
		join(i, i * 10);

	CHECK(mh_net_session_begin_race(&host, 100, NULL) == MH_ERROR_NONE);
	pump(100);
	configure_replication(&host);

	for (i = 0; i < 7; i++)
		configure_replication(&clients[i]);

	CHECK(mh_net_session_loaded(&host, 101, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 7; i++)
		CHECK(mh_net_session_loaded(&clients[i], 101, NULL) ==
			MH_ERROR_NONE);

	CHECK(mh_net_session_status(&host, 3, 17, 29, NULL) == MH_ERROR_NONE);

	for (tick = 110; tick <= 5000 && failures == 0; tick += 10) {
		CHECK(service_frame(&host, tick, NULL) == MH_ERROR_NONE);

		for (i = 0; i < 7; i++)
			CHECK(service_frame(&clients[i], tick, NULL) ==
				MH_ERROR_NONE);

		pump(tick);

		for (i = 0; i < 8; i++) {
			session = i == 0 ? &host : &clients[i - 1];

			for (j = 0; j < 8; j++)
				if (i != j)
					CHECK(mh_net_session_sample(session, j,
						tick, &playback,
						NULL) == MH_ERROR_NONE);
		}
	}

	for (i = 0; i < 8; i++) {
		session = i == 0 ? &host : &clients[i - 1];
		CHECK(session->phase == MH_NET_RACING &&
			session->replication.history[i].count == 0);
		CHECK(session->replication.status[3].value_a == 17 &&
			session->replication.status[3].value_b == 29);

		for (j = 0; j < 8; j++)
			if (i != j) {
				CHECK(session->replication.history[j].count ==
					10);
				CHECK(mh_net_session_sample(session, j, 5000,
					&playback,
					NULL) == MH_ERROR_NONE);
				CHECK(playback.ready && playback.updated &&
					!playback.stale);
				CHECK(playback.vehicle.position[0] == j * 100);
			}
	}

	CHECK(clients[1].replication.history[1].entries[0].vehicle.flags ==
		0x701);
	CHECK(clients[0].replication.history[7].entries[0].vehicle.rpm ==
		7000); /* far defaults */
	CHECK(service_frame(&host, 5100, NULL) == MH_ERROR_NONE);
	CHECK(snapshot_type(1, 0) == MH_NET_CAR &&
		snapshot_type(1, 7) == MH_NET_CAR_FAR);
	CHECK(snapshot_type(7, 0) == MH_NET_CAR_FAR &&
		snapshot_type(7, 6) == MH_NET_CAR);
	CHECK(snapshot_type(1, 1) == 0);
	pump(5100);

	/* The recipient's viewed car, not its own car, determines detail. */
	vehicle = vehicle_snapshot(1);
	CHECK(mh_net_session_publish(&clients[0], &vehicle, 7, NULL) ==
		MH_ERROR_NONE);
	CHECK(service_frame(&clients[0], 5101, NULL) == MH_ERROR_NONE);
	pump(5101);
	CHECK(host.replication.view[1] == 7);
	CHECK(service_frame(&host, 5200, NULL) == MH_ERROR_NONE);
	CHECK(snapshot_type(1, 0) == MH_NET_CAR_FAR &&
		snapshot_type(1, 7) == MH_NET_CAR);
	pump(5200);

	/*
	 * Coordinates outside compact i16 range use full snapshots, without
	 * wrapping.
	 */
	vehicle = vehicle_snapshot(0);
	vehicle.position[0] = 3000;
	CHECK(mh_net_session_publish(&host, &vehicle, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(service_frame(&host, 5300, NULL) == MH_ERROR_NONE &&
		snapshot_type(1, 0) == MH_NET_CAR);
	pump(5300);
	CHECK(clients[0].replication.history[0]
			.entries[0]
			.vehicle.position[0] == 3000);

	/*
	 * Stamps survive host relay; duplicate/old packets do not renew
	 * liveness or view.
	 */
	stamp = host.replication.history[1].entries[0].stamp;
	CHECK(clients[1].replication.history[1].entries[0].stamp == stamp);
	received = host.replication.history[1].received_ms;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CAR;
	message.player = 1;
	message.stamp = stamp;
	message.view_player = 0;
	vehicle = vehicle_snapshot(1);
	CHECK(mh_net_vehicle_encode(&vehicle, message.car, 30, NULL) ==
		MH_ERROR_NONE);
	CHECK(deliver(&host, 2, &message, 0, 5301, NULL) == MH_ERROR_NONE);
	CHECK(host.replication.history[1].received_ms == received &&
		host.replication.view[1] == 7);
	message.stamp--;
	CHECK(deliver(&host, 2, &message, 0, 5302, NULL) == MH_ERROR_NONE);
	CHECK(host.replication.history[1].received_ms == received);

	/* Ownership and full-batch rollback include histories and clocks. */
	message.stamp = 5400;
	saved = host;
	message.player = 2;
	CHECK(deliver(&host, 2, &message, 0, 5400, NULL) != MH_ERROR_NONE &&
		memcmp(&host, &saved, sizeof(host)) == 0);
	message.player = 1;
	message.view_player = 255;
	CHECK(deliver(&host, 2, &message, 0, 5400, NULL) != MH_ERROR_NONE &&
		memcmp(&host, &saved, sizeof(host)) == 0);
	message.view_player = 1;
	mh_net_batch_reset(&batch);
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE);
	message.player = 2;
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_receive(&host, 2, batch.data, batch.size, 0, 5400,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&host, &saved, sizeof(host)) == 0);
	message.player = 1;
	message.type = MH_NET_CAR_FAR;
	CHECK(deliver(&host, 2, &message, 0, 5400, NULL) != MH_ERROR_NONE);
	message.type = MH_NET_STATUS;
	CHECK(deliver(&host, 2, &message, 0, 5400, NULL) != MH_ERROR_NONE);
	message.type = MH_NET_CAR;
	saved = clients[0];
	CHECK(deliver(&clients[0], 1, &message, 0, 5400, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&clients[0], &saved, sizeof(saved)) == 0);
	vehicle = vehicle_snapshot(1);
	vehicle.position[0] = 1000001;
	CHECK(mh_net_session_publish(&clients[0], &vehicle, 1, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&clients[0], &saved, sizeof(saved)) == 0);

	/*
	 * A full outbox rolls back status, send clocks and already queued
	 * recipients.
	 */
	CHECK(service_frame(&host, 5400, NULL) == MH_ERROR_NONE);

	while (host.out_count + 7 <= MH_NET_OUTBOX)
		CHECK(mh_net_session_chat(&host, "queued", 0, NULL) ==
			MH_ERROR_NONE);

	saved = host;
	CHECK(service_frame(&host, 6500, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&host, &saved, sizeof(host)) == 0);
	pump(5400);

	/* Status staleness recovers only when fresh car data arrives. */
	CHECK(service_frame(&host, 6500, NULL) == MH_ERROR_NONE);
	pump(6500);
	CHECK((clients[0].replication.status[1].flags & 8) != 0);
	CHECK(service_frame(&clients[0], 6501, NULL) == MH_ERROR_NONE);
	pump(6501);
	CHECK(service_frame(&host, 7501, NULL) == MH_ERROR_NONE);
	pump(7501);
	CHECK((clients[0].replication.status[1].flags & 8) == 0);

	/*
	 * Departure clears histories and view targets; late status cannot
	 * resurrect a slot.
	 */
	CHECK(mh_net_session_leave(&clients[6], NULL) == MH_ERROR_NONE);
	pump(7510);
	CHECK(host.replication.history[7].count == 0 &&
		host.replication.view[1] == 1);
	message.type = MH_NET_STATUS;
	message.status[7].flags = 1;
	CHECK(deliver(&clients[0], 1, &message, 0, 7511, NULL) ==
		MH_ERROR_NONE);
	CHECK(clients[0].roster.players[7].flags == 0 &&
		clients[0].replication.status[7].flags == 0);

	/*
	 * Discard queued unreliable snapshots before sending the reliable lobby
	 * transition.
	 */
	CHECK(service_frame(&host, 7600, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE);
	pump(7600);
	CHECK(!host.replication.configured &&
		host.replication.history[1].count == 0);
	message.type = MH_NET_CAR;
	message.player = 0;
	message.stamp = 7600;
	CHECK(deliver(&clients[0], 1, &message, 0, 7601, NULL) ==
			MH_ERROR_NONE &&
		!clients[0].replication.configured);
	CHECK(service_frame(&host, 7602, NULL) == MH_ERROR_NONE);
	pump(7602);
	CHECK(host.return_pending == 0);
	CHECK(mh_net_session_begin_race(&host, 7700, NULL) == MH_ERROR_NONE);
	pump(7700);
	configure_replication(&host);
	CHECK(host.replication.history[1].count == 0 &&
		host.replication.sent_ms[1] == 0);
}

static void
replication_rates(void)
{
	struct mh_net_profile profile;
	double position[MH_NET_PLAYERS][3];

	setup();
	join(0, 0);
	join(1, 1);
	profile = clients[0].config.profile;
	profile.update_rate = 5;
	CHECK(mh_net_session_profile(&clients[0], &profile, NULL) ==
		MH_ERROR_NONE);
	pump(2);
	profile = clients[1].config.profile;
	profile.update_rate = 30;
	CHECK(mh_net_session_profile(&clients[1], &profile, NULL) ==
		MH_ERROR_NONE);
	pump(3);
	CHECK(mh_net_session_begin_race(&host, 10, NULL) == MH_ERROR_NONE);
	pump(10);
	saved = host;
	memset(position, 0, sizeof(position));
	position[1][2] = 1000001;
	CHECK(mh_net_session_configure_replication(&host, 180,
		(const double (*)[3])position, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&host, &saved, sizeof(host)) == 0);
	position[1][2] = 0;
	CHECK(mh_net_session_configure_replication(&host, 0,
		(const double (*)[3])position, NULL) != MH_ERROR_NONE);
	configure_replication(&host);
	configure_replication(&clients[0]);
	configure_replication(&clients[1]);
	CHECK(mh_net_session_loaded(&host, 11, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(&clients[0], 11, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(&clients[1], 11, NULL) == MH_ERROR_NONE);
	CHECK(service_frame(&clients[1], 33, NULL) == MH_ERROR_NONE &&
		clients[1].out_count == 0);
	CHECK(service_frame(&clients[1], 33.01, NULL) == MH_ERROR_NONE &&
		clients[1].out_count == 1);
	pump(33.01);
	CHECK(service_frame(&clients[1], 66, NULL) == MH_ERROR_NONE &&
		clients[1].out_count == 0);
	CHECK(service_frame(&clients[1], 66.01, NULL) == MH_ERROR_NONE &&
		clients[1].out_count == 1);
	pump(66.01);
	CHECK(service_frame(&clients[0], 200, NULL) == MH_ERROR_NONE &&
		clients[0].out_count == 0);
	CHECK(service_frame(&clients[0], 200.01, NULL) == MH_ERROR_NONE &&
		clients[0].out_count == 1);
	pump(200.01);
	CHECK(service_frame(&host, 1001, NULL) == MH_ERROR_NONE);
	pump(1001);
	CHECK(host.ready_mask == 7);
	CHECK(service_frame(&host, 1034, NULL) == MH_ERROR_NONE &&
		host.out_count == 0);
	CHECK(service_frame(&host, 1034.01, NULL) == MH_ERROR_NONE);
	CHECK(snapshot_type(2, 0) == MH_NET_CAR_FAR &&
		snapshot_type(1, 0) == 0);
	pump(1034.01);
	CHECK(service_frame(&host, 1201, NULL) == MH_ERROR_NONE);
	CHECK(snapshot_type(1, 0) == 0);
	pump(1201);
	CHECK(service_frame(&host, 1201.01, NULL) == MH_ERROR_NONE);
	CHECK(snapshot_type(1, 0) == MH_NET_CAR);
	pump(1201.01);
}

static void
stream_boundaries(void)
{
	double source[3], viewed[3];

	memset(source, 0, sizeof(source));
	memset(viewed, 0, sizeof(viewed));
	CHECK(mh_net_snapshot_due(0, 0, 1) == -1 &&
		mh_net_snapshot_due(0, 256, 1) == -1);
	CHECK(mh_net_snapshot_due(0, 15, -1) == -1 &&
		mh_net_snapshot_due(0, 15, HUGE_VAL) == -1);
	CHECK(mh_net_snapshot_due(0, 15, 66) == 0 &&
		mh_net_snapshot_due(0, 15, 66.01) == 1);
	CHECK(mh_net_snapshot_due(MH_U32_MAX, 15, 65) == 0 &&
		mh_net_snapshot_due(MH_U32_MAX, 15, 65.01) == 1);
	CHECK(mh_net_snapshot_detailed(source, viewed, 180) == 1);
	CHECK(mh_net_snapshot_detailed(source, viewed, 0) == -1);
	CHECK(mh_net_snapshot_detailed(source, viewed, 1e-100) == -1);
	CHECK(mh_net_snapshot_detailed(source, viewed, HUGE_VAL) == -1);
	CHECK(mh_net_snapshot_detailed(NULL, viewed, 180) == -1);
	viewed[0] = 1000001;
	CHECK(mh_net_snapshot_detailed(source, viewed, 180) == -1);
	viewed[0] = HUGE_VAL;
	CHECK(mh_net_snapshot_detailed(source, viewed, 180) == -1);
	viewed[0] = 139;
	CHECK(mh_net_snapshot_detailed(source, viewed, 180) == 0);
	viewed[0] = 138;
	CHECK(mh_net_snapshot_detailed(source, viewed, 180) == 1);
#if LDBL_MANT_DIG > DBL_MANT_DIG
	/* Extended precision keeps this sample inside the detail boundary. */
	viewed[0] = 123.07691955566406;
	viewed[1] = .02944098226726055;
	CHECK(mh_net_snapshot_detailed(source, viewed, 160) == 1);
#endif
}

static void
round_completion(unsigned int mode)
{
	static const unsigned int points[8] = {12, 10, 8, 6, 4, 3, 2, 1};
	unsigned int i, positions[8], chat_count;

	setup();

	for (i = 0; i < 7; i++)
		join(i, i * 10);

	host.config.mode = mode;
	CHECK(mh_net_session_begin_race(&host, 100, NULL) == MH_ERROR_NONE);
	pump(100);
	CHECK(mh_net_session_loaded(&host, 101, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 7; i++)
		CHECK(mh_net_session_loaded(&clients[i], 101, NULL) ==
			MH_ERROR_NONE);

	CHECK(service_frame(&host, 1001, NULL) == MH_ERROR_NONE);
	pump(1001);
	CHECK(service_frame(&host, 1002, NULL) == MH_ERROR_NONE);
	pump(1002);
	CHECK(service_frame(&host, 3003, NULL) == MH_ERROR_NONE);
	pump(3003);
	CHECK(host.phase == MH_NET_RACING);
	chat_count = clients[2].chat_count;
	CHECK(mh_net_session_notice(&host, 3, "Time left: 30", 0xffffffUL,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_notice(&host, 0, "Did not finish!", 0xffffffUL,
		NULL) == MH_ERROR_NONE);
	pump(3004);
	CHECK(host.notice_revision == 1 &&
		strcmp(host.notice.text, "Did not finish!") == 0);

	for (i = 0; i < 7; i++)
		CHECK(clients[i].notice_revision == (i == 2 ? 1UL : 0UL));

	CHECK(clients[2].chat_count == chat_count &&
		strcmp(clients[2].notice.text, "Time left: 30") == 0);
	CHECK(mh_net_session_notice(&clients[0], 0, "spoof", 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_session_notice(&host, 8, "bad slot", 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_session_notice(&host, 1, NULL, 0, NULL) != MH_ERROR_NONE);

	for (i = 0; i < 8; i++) {
		positions[i] = 8 - i;
		host.lobby.slots[i].profile.values[4] = 10 + i;
		host.lobby.slots[i].profile.values[5] = i * 100;
	}

	CHECK(mh_net_session_finish_race(&clients[0], positions, NULL) !=
		MH_ERROR_NONE);
	host.player = MH_NET_PLAYERS;
	saved = host;
	CHECK(mh_net_session_finish_race(&host, positions, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&host, &saved, sizeof(host)) == 0);
	CHECK(mh_net_session_notice(&host, 1, "bad host slot", 0, NULL) !=
		MH_ERROR_NONE);
	host.player = 0;
	saved = host;
	positions[7] = 2;
	CHECK(mh_net_session_finish_race(&host, positions, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&host, &saved, sizeof(host)) == 0);
	positions[7] = 1;

	/*
	 * Fill the outbox until fourteen departure/roster packets cannot fit.
	 * No score counters or return-wait bits may commit on failure.
	 */
	for (i = 0; i < MH_NET_OUTBOX / 7; i++)
		CHECK(mh_net_session_chat(&host, "queued", 0, NULL) ==
			MH_ERROR_NONE);

	saved = host;
	CHECK(mh_net_session_finish_race(&host, positions, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&host, &saved, sizeof(host)) == 0);
	drain(&host);
	CHECK(mh_net_session_finish_race(&host, positions, NULL) ==
		MH_ERROR_NONE);
	pump(3005);
	CHECK(host.phase == MH_NET_LOBBY && host.notice_revision == 0);
	CHECK(host.config.profile.values[4] == 11 &&
		host.config.profile.values[5] == 1);

	for (i = 0; i < 8; i++) {
		CHECK(host.roster.players[i].values[4] == 11 + i);
		CHECK(host.roster.players[i].values[5] ==
			i * 100 + points[7 - i]);
	}

	for (i = 0; i < 7; i++) {
		CHECK(clients[i].phase == MH_NET_LOBBY &&
			clients[i].notice_revision == 0);
		CHECK(memcmp(&clients[i].roster, &host.roster,
			sizeof(host.roster)) == 0);
		CHECK(clients[i].config.profile.values[4] ==
			host.roster.players[i + 1].values[4]);
	}

	saved = host;
	CHECK(mh_net_session_finish_race(&host, positions, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&host, &saved, sizeof(host)) == 0);
	CHECK(host.return_pending == 254);
	CHECK(service_frame(&host, 3006, NULL) == MH_ERROR_NONE);
	pump(3006);
	CHECK(host.return_pending == 0);
	CHECK(mh_net_session_begin_race(&host, 3010, NULL) == MH_ERROR_NONE);
	pump(3010);
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE);
	pump(3011);

	for (i = 0; i < 8; i++)
		CHECK(host.roster.players[i].values[4] == 11 + i);
}

static void
announcements(void)
{
	struct mh_net_announcement events[16];
	unsigned int i;
	unsigned long revision;

	setup();

	for (i = 0; i < 7; i++)
		join(i, i * 10);

	host.config.mode = 1;
	CHECK(mh_net_session_begin_race(&host, 100, NULL) == MH_ERROR_NONE);
	pump(100);
	memset(events, 0, sizeof(events));

	for (i = 0; i < 8; i++) {
		events[i * 2].recipients = 1U << i;
		events[i * 2].color = 0xffffffUL;
		sprintf(events[i * 2].text,
			"You have been eliminated by Driver%u", 7 - i);
		events[i * 2 + 1].recipients = 255;
		events[i * 2 + 1].chat = 1;
		events[i * 2 + 1].color = 0xe06820UL;
		sprintf(events[i * 2 + 1].text, "Driver%u has been eliminated!",
			i);
	}

	CHECK(mh_net_session_announcements(&host, events, 16, NULL) ==
			MH_ERROR_NONE &&
		host.out_count == 7);
	CHECK(host.notice_revision == 1 && host.chat_count == 8);
	pump(101);

	for (i = 0; i < 7; i++) {
		CHECK(clients[i].notice_revision == 1 &&
			clients[i].chat_count == 8);
		CHECK(strcmp(clients[i].notice.text,
			events[(i + 1) * 2].text) == 0);
		CHECK(memcmp(clients[i].chat, host.chat, sizeof(host.chat)) ==
			0);
	}

	revision = host.notice_revision;
	CHECK(mh_net_session_announcements(&host, events, 16, NULL) ==
		MH_ERROR_NONE);
	pump(102);
	CHECK(host.notice_revision == revision);

	for (i = 0; i < 7; i++)
		CHECK(clients[i].notice_revision == 1);

	for (i = 0; i < MH_NET_OUTBOX / 7; i++)
		CHECK(mh_net_session_chat(&host, "queued", 0, NULL) ==
			MH_ERROR_NONE);

	saved = host;
	CHECK(mh_net_session_announcements(&host, events, 16, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&host, &saved, sizeof(host)) == 0);
	drain(&host);
	saved = host;
	events[15].recipients = 256;
	CHECK(mh_net_session_announcements(&host, events, 16, NULL) !=
		MH_ERROR_NONE);
	events[15].recipients = 255;
	memset(events[15].text, 'x', sizeof(events[15].text));
	CHECK(mh_net_session_announcements(&host, events, 16, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_session_announcements(&host, events, 17, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_session_announcements(&host, NULL, 1, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_session_announcements(&clients[0], events, 16, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&host, &saved, sizeof(host)) == 0);
	configure_replication(&host);
	CHECK(mh_net_session_loaded(&host, 103, NULL) == MH_ERROR_NONE);
	host.replication.eliminated = 1;
	CHECK(service_frame(&host, 1001, NULL) == MH_ERROR_NONE &&
		(host.replication.status[0].flags & 8));
	drain(&host);
	host.replication.eliminated = 0;
	CHECK(service_frame(&host, 2002, NULL) == MH_ERROR_NONE &&
		!(host.replication.status[0].flags & 8));
}

static void
announcement_batches(void)
{
	struct mh_net_announcement events[16];
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_message message;
	const struct mh_net_outgoing *packet;
	unsigned int i, recipient, counts[2];
	int status;

	setup();
	join(0, 0);
	join(1, 0);
	CHECK(mh_net_session_begin_race(&host, 1, NULL) == MH_ERROR_NONE);
	pump(1);
	clear_chat_windows();
	memset(events, 0, sizeof(events));

	for (i = 0; i < 16; i++) {
		events[i].recipients = 7;
		events[i].chat = (int)(i % 2);
		events[i].color = i;
		memset(events[i].text, 'A' + (int)i, 79);
	}

	CHECK(mh_net_session_announcements(&host, events, 16, NULL) ==
		MH_ERROR_NONE);
	CHECK(host.out_count == 4 && host.chat_count == 8);
	CHECK(!strcmp(host.notice.text, events[14].text));
	memset(counts, 0, sizeof(counts));

	/* Each recipient gets eleven 89-byte messages, then five more. */
	for (i = 0; i < host.out_count; i++) {
		packet = &host.outgoing[(host.out_head + i) % MH_NET_OUTBOX];
		CHECK(packet->peer == 2 + i / 2 && packet->reliable &&
			!packet->disconnect);
		CHECK(packet->size == (i % 2 == 0 ? 979 : 445));
		recipient = i / 2;

		if (recipient >= 2)
			break;

		CHECK(mh_net_begin(packet->data, packet->size, &reader, NULL) ==
			MH_ERROR_NONE);

		while ((status = mh_net_next(&reader, &frame)) == 1) {
			CHECK(mh_net_decode(&frame, &message, NULL) ==
				MH_ERROR_NONE);

			if (counts[recipient] >= 16) {
				CHECK(0);
				break;
			}

			CHECK(message.player == 0 &&
				message.color == counts[recipient]);
			CHECK(message.type ==
				(counts[recipient] % 2 ? MH_NET_CHAT
						: MH_NET_NOTICE));
			CHECK(!strcmp(
				message.text, events[counts[recipient]].text));
			counts[recipient]++;
		}

		CHECK(status == 0);
	}

	CHECK(counts[0] == 16 && counts[1] == 16);
	pump(2);
	CHECK(!strcmp(clients[0].notice.text, host.notice.text));
	CHECK(!strcmp(clients[1].notice.text, host.notice.text));
	CHECK(!memcmp(clients[0].chat, host.chat, sizeof(host.chat)));
	CHECK(!memcmp(clients[1].chat, host.chat, sizeof(host.chat)));

	/* Failure on the last packet must undo local text and earlier peers. */
	for (i = 0; i < MH_NET_OUTBOX / 2; i++)
		CHECK(mh_net_session_chat(&host, "queued", 0, NULL) ==
			MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		mh_net_session_sent(&host);

	saved = host;
	CHECK(mh_net_session_announcements(&host, events, 16, NULL) !=
		MH_ERROR_NONE);
	CHECK(!memcmp(&host, &saved, sizeof(host)));
}

static void
lobby_commands(void)
{
	static const struct mh_lobby_command_name tracks[] = {
		{"Track1.trk"}, {"Beta Track"}};

	static const struct mh_lobby_command_name cars[] = {
		{"Kobra.car"}, {"Beta Car"}};

	static const struct mh_lobby_command_name horns[] = {
		{"Horn1.chf"}, {"Two Horn"}};

	struct mh_lobby_command_catalog catalog;
	struct mh_lobby_command_state host_console, client_console, before;
	struct mh_lobby_command_output output, output_before;
	struct mh_net_session_config settings;
	unsigned int i;
	unsigned long revision;
	char bad[256];

	memset(&host_console, 0, sizeof(host_console));
	memset(&client_console, 0, sizeof(client_console));
	memset(&output, 0, sizeof(output));
	catalog.tracks = tracks;
	catalog.cars = cars;
	catalog.horns = horns;
	catalog.track_count = catalog.track_limit = catalog.car_count =
		catalog.car_limit = catalog.horn_count = 2;
	setup();
	join(0, 0);
	join(1, 10);
	clear_chat_windows();
	CHECK(mh_net_session_command(&host, &host_console, &catalog,
		"/gamemode elimination", &output, NULL) == MH_ERROR_NONE);
	CHECK(host.config.mode == 1 && host.roster.mode == 1 &&
		host.out_count == 0);
	CHECK(clients[0].roster.mode == 0 && output.count == 1);
	CHECK(mh_net_session_command(&host, &host_console, &catalog,
		"/track beta track", &output, NULL) == MH_ERROR_NONE);
	pump(20);
	CHECK(!strcmp(host.config.track, "Beta Track") &&
		!strcmp(clients[0].roster.track, "Beta Track"));
	CHECK(clients[1].roster.mode == 1);
	CHECK(mh_net_session_command(&host, &host_console, &catalog, "/laps -1",
		&output, NULL) == MH_ERROR_NONE);
	CHECK(host.config.laps == 3 && host.out_count == 0);
	CHECK(mh_net_session_command(&host, &host_console, &catalog, "/laps 9",
		&output, NULL) == MH_ERROR_NONE);
	pump(21);
	CHECK(clients[0].roster.laps == 9 && clients[1].roster.laps == 9);
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/track Track1.trk", &output, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(clients[0].roster.track, "Beta Track") &&
		clients[0].out_count == 0);
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/name Renamed", &output, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(clients[0].config.profile.name, "Renamed") &&
		!strcmp(clients[0].roster.players[1].name, "Renamed"));
	pump(22);
	CHECK(!strcmp(host.roster.players[1].name, "Renamed"));
	CHECK(!strcmp(clients[1].roster.players[1].name, "Renamed"));
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/me waves", &output, NULL) == MH_ERROR_NONE);
	CHECK(clients[0].chat_count == 1 && output.count == 2);
	pump(23);
	CHECK(clients[0].chat_count == 1 && host.chat_count == 1 &&
		clients[1].chat_count == 1);
	CHECK(!strcmp(host.chat[0].text, "Renamed waves") &&
		host.chat[0].color == 0xffffffUL);
	CHECK(mh_net_session_command(&host, &host_console, &catalog, "/help",
		&output, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 24 && host.chat_count == 1 &&
		host.out_count == 0);
	CHECK(mh_net_session_command(&host, &host_console, &catalog,
		"/password secret", &output, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(host.config.password, "secret") &&
		!strcmp(host.lobby.password, "secret"));
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/password secret", &output, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(clients[0].config.password, "secret") &&
		clients[0].out_count == 0);
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/minpredict 8", &output, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/maxpredict 2", &output, NULL) == MH_ERROR_NONE);
	CHECK(clients[0].config.prediction.minimum == 8 &&
		clients[0].config.prediction.maximum == 2 &&
		clients[0].out_count == 0);
	CHECK(host.config.prediction.minimum == 1 &&
		host.config.prediction.maximum == 5);
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/modem", &output, NULL) == MH_ERROR_NONE);
	CHECK(clients[0].config.profile.update_rate == 10 &&
		clients[0].config.prediction.minimum == 2 &&
		clients[0].config.prediction.maximum == 6);
	pump(24);
	CHECK(host.roster.players[1].update_rate == 10 &&
		clients[1].roster.players[1].update_rate == 10);
	CHECK(host.config.prediction.minimum == 1 &&
		host.config.prediction.maximum == 5);
	CHECK(mh_net_session_command(&host, &host_console, &catalog, "/LAN",
		&output, NULL) == MH_ERROR_NONE);
	pump(25);
	CHECK(host.config.profile.update_rate == 30 &&
		clients[0].roster.players[0].update_rate == 30);
	CHECK(clients[0].config.prediction.minimum == 2 &&
		clients[0].config.prediction.maximum == 6);

	settings = session_config(3);
	strcpy(settings.password, "secret");
	CHECK(mh_net_session_init(0, &settings, &clients[2], NULL) ==
		MH_ERROR_NONE);
	join(2, 30);
	CHECK(clients[2].phase == MH_NET_LOBBY);
	join(3, 31);
	CHECK(clients[3].phase == MH_NET_REJECTED);
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/record Race001", &output, NULL) == MH_ERROR_NONE);
	pump(32);
	CHECK(!strcmp(client_console.demo_path, "demos\\Race001.mde") &&
		(host.roster.players[1].flags & 4));
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/rate -1", &output, NULL) == MH_ERROR_NONE);
	pump(33);
	CHECK(host.roster.players[1].update_rate == 255 &&
		(clients[1].roster.players[1].flags & 4));
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/car beta car", &output, NULL) == MH_ERROR_NONE);
	pump(34);
	CHECK(!strcmp(host.roster.players[1].car, "Beta Car"));
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/horn TWO HORN", &output, NULL) == MH_ERROR_NONE);
	pump(35);
	CHECK(!strcmp(host.roster.players[1].horn, "TWO HORN"));
	revision = host.roster_revision;
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/car missing", &output, NULL) == MH_ERROR_NONE);
	CHECK(clients[0].out_count == 1);
	pump(36);
	CHECK(host.roster_revision > revision &&
		!strcmp(host.roster.players[1].car, "Beta Car"));
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/color -1 256 257", &output, NULL) == MH_ERROR_NONE);
	pump(37);
	CHECK(host.roster.players[1].values[0] == 0xff0001UL &&
		output.events[2].kind == MH_LOBBY_PALETTE);
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/record", &output, NULL) == MH_ERROR_NONE);
	pump(38);
	CHECK(!(host.roster.players[1].flags & 4) &&
		!strcmp(client_console.demo_path, "demos\\Race001.mde"));
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/reconnect", &output, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 1 &&
		output.events[0].kind == MH_LOBBY_RECONNECT &&
		clients[0].phase == MH_NET_LOBBY);

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		host.lobby.slots[i].profile.values[4] = 3;
		host.lobby.slots[i].profile.values[5] = i + 10;
	}

	host.config.profile.values[4] = 3;
	host.config.profile.values[5] = 10;
	host_console.confirm_clear = 1;
	CHECK(mh_net_session_command(&host, &host_console, &catalog, "/quit",
		&output, NULL) == MH_ERROR_NONE);
	CHECK(!host_console.confirm_clear && host.phase == MH_NET_LOBBY &&
		host.lobby.slots[0].profile.values[4] == 3);
	host_console.confirm_clear = 1;
	CHECK(mh_net_session_command(&host, &host_console, &catalog, "yes",
		&output, NULL) == MH_ERROR_NONE);
	pump(40);
	CHECK(host.config.profile.values[4] == 0 &&
		host.config.profile.values[5] == 0);

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		CHECK(host.lobby.slots[i].profile.values[4] == 0 &&
			host.lobby.slots[i].profile.values[5] == 0);
		CHECK(clients[0].roster.players[i].values[4] == 0 &&
			clients[0].roster.players[i].values[5] == 0);
	}

	CHECK(mh_net_session_command(&host, &host_console, &catalog,
		"/kick Renamed", &output, NULL) == MH_ERROR_NONE);
	pump(41);
	CHECK(clients[0].kicked && clients[0].phase == MH_NET_CLOSED &&
		!(host.roster.players[1].flags & 3));
	CHECK(clients[1].phase == MH_NET_LOBBY);

	setup();
	join(0, 0);
	memset(&client_console, 0, sizeof(client_console));

	for (i = 0; i < MH_NET_OUTBOX; i++)
		CHECK(mh_net_session_chat(&clients[0], "queued", 0, NULL) ==
			MH_ERROR_NONE);

	saved = clients[0];
	before = client_console;
	output_before = output;
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/name Blocked", &output, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&clients[0], &saved, sizeof(saved)) &&
		!memcmp(&client_console, &before, sizeof(client_console)) &&
		!memcmp(&output, &output_before, sizeof(output)));
	memset(bad, 'x', sizeof(bad));
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		bad, &output, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&clients[0], &saved, sizeof(saved)) &&
		!memcmp(&client_console, &before, sizeof(client_console)) &&
		!memcmp(&output, &output_before, sizeof(output)));
	memset(&host_console, 0, sizeof(host_console));

	for (i = 0; i < MH_NET_OUTBOX; i++)
		CHECK(mh_net_session_chat(&host, "queued", 0, NULL) ==
			MH_ERROR_NONE);

	saved = host;
	before = host_console;
	output_before = output;
	CHECK(mh_net_session_command(&host, &host_console, &catalog,
		"/track Beta Track", &output, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&host, &saved, sizeof(saved)) &&
		!memcmp(&host_console, &before, sizeof(host_console)) &&
		!memcmp(&output, &output_before, sizeof(output)));
	drain(&host);
	drain(&clients[0]);
	CHECK(mh_net_session_command(&clients[0], &client_console, &catalog,
		"/quit", &output, NULL) == MH_ERROR_NONE);
	CHECK(client_console.quit && !client_console.connected &&
		clients[0].phase == MH_NET_CLOSED);
	pump(1);
	CHECK(!(host.roster.players[1].flags & 3));

	/*
	 * Queries preserve fixed-field trailing bytes. Setters still use the
	 * original byte limits and retain bytes beyond the new terminator.
	 */
	settings = session_config(0);
	memset(settings.password, 'P', 15);
	settings.password[15] = 0;
	memcpy(settings.profile.team, "A\0CDE", 5);
	CHECK(mh_net_session_init(1, &settings, &host, NULL) == MH_ERROR_NONE);
	memset(&host_console, 0, sizeof(host_console));
	CHECK(mh_net_session_command(&host, &host_console, &catalog, "/team",
		&output, NULL) == MH_ERROR_NONE);
	CHECK(!memcmp(host.config.password, settings.password, 16) &&
		!memcmp(host.config.profile.team, "A\0CDE", 5));
	CHECK(!strcmp(output.events[0].text, "Current team: A"));
	CHECK(mh_net_session_command(&host, &host_console, &catalog,
		"/team ABCD", &output, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(host.config.profile.team, "ABCD"));
}

static long double
key_clock(void *context)
{
	return *(const double *)context;
}

static enum mh_error_code
key_frame(struct mh_net_session *session, struct mh_lobby_keys *keys,
	const unsigned char events[MH_KEY_COUNT], double now,
	struct mh_lobby_key_output *out, struct mh_error *error_out)
{
	static const struct mh_lobby_key_name tracks[] = {
		{"Track1.trk"}, {"Beta Track"}};

	static const struct mh_lobby_key_name cars[] = {
		{"Kobra.car"}, {"Beta Car"}};

	struct mh_lobby_key_catalog catalog;
	struct mh_lobby_key_input input;

	memset(&input, 0, sizeof(input));
	input.events = events;
	input.clock_ms = key_clock;
	input.clock_context = &now;
	input.clipboard = (const unsigned char *)"Clipboard";
	input.clipboard_size = 9;
	catalog.tracks = tracks;
	catalog.cars = cars;
	catalog.track_count = catalog.car_count = catalog.car_limit = 2;

	return mh_net_session_keys(
		session, keys, &catalog, &input, now, out, error_out);
}

static void
lobby_keys(void)
{
	struct mh_lobby_keys host_keys, client_keys, before;
	struct mh_lobby_key_output output, output_before;
	struct mh_net_profile profile;
	struct mh_net_message message;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	const struct mh_net_outgoing *packet;
	unsigned char events[MH_KEY_COUNT];
	unsigned int i;

	setup();
	join(0, 0);
	join(1, 10);
	memset(&host_keys, 0, sizeof(host_keys));
	memset(&client_keys, 0, sizeof(client_keys));
	memset(events, 0, sizeof(events));
	events[0x3b] = events[0x3d] = 1;
	CHECK(key_frame(&host, &host_keys, events, 100, &output, NULL) ==
			MH_ERROR_NONE &&
		!output.count && !host.out_count);
	CHECK(!strcmp(host.config.track, "Beta Track") &&
		host.roster.laps == 5);
	CHECK(!strcmp(clients[0].roster.track, "Track1.trk") &&
		clients[0].roster.laps == 3);
	memset(events, 0, sizeof(events));
	events[0x40] = 1;
	CHECK(key_frame(&clients[0], &client_keys, events, 110, &output,
		NULL) == MH_ERROR_NONE &&
		!output.count && !clients[0].out_count);
	CHECK(!strcmp(clients[0].config.profile.car, "Beta Car") &&
		!strcmp(host.roster.players[1].car, "Kobra.car"));

	/*
	 * Another profile broadcasts an old copy of our unsent car selection.
	 */
	profile = clients[1].config.profile;
	strcpy(profile.name, "Other");
	CHECK(mh_net_session_profile(&clients[1], &profile, NULL) ==
		MH_ERROR_NONE);
	pump(120);
	CHECK(!strcmp(clients[0].roster.players[1].car, "Kobra.car"));
	memset(events, 0, sizeof(events));
	CHECK(key_frame(&clients[0], &client_keys, events, 121, &output,
		NULL) == MH_ERROR_NONE &&
		!output.count);
	CHECK(!strcmp(client_keys.values.car, "Beta Car") &&
		!strcmp(clients[0].roster.players[1].car, "Beta Car"));
	CHECK(client_keys.changed[MH_LOBBY_CAR_TIMER] == 110 &&
		!strcmp(host.roster.players[1].car, "Kobra.car"));
	CHECK(key_frame(&host, &host_keys, events, 600, &output, NULL) ==
			MH_ERROR_NONE &&
		!output.count && !host.out_count);
	CHECK(key_frame(&host, &host_keys, events, 600.5, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 2 && host.out_count == 4);
	pump(601);
	CHECK(!strcmp(clients[1].roster.track, "Beta Track") &&
		clients[1].roster.laps == 5);
	CHECK(key_frame(&clients[0], &client_keys, events, 610, &output,
		NULL) == MH_ERROR_NONE &&
		!output.count);
	events[0x41] = 1;
	CHECK(key_frame(&clients[0], &client_keys, events, 611, &output,
		NULL) == MH_ERROR_NONE &&
		output.count == 1);
	CHECK(output.actions[0].values.gear == 0 &&
		clients[0].config.profile.gear_mode == 1);
	packet = mh_net_session_outgoing(&clients[0]);
	CHECK(packet != NULL);

	if (packet != NULL) {
		CHECK(mh_net_begin(packet->data, packet->size, &reader, NULL) ==
				MH_ERROR_NONE &&
			mh_net_next(&reader, &frame) == 1);
		CHECK(mh_net_decode(&frame, &message, NULL) == MH_ERROR_NONE &&
			message.type == MH_NET_PROFILE_UPDATE);
		CHECK(message.profile.gear_mode == 0 &&
			!strcmp(message.profile.car, "Beta Car"));
	}

	pump(611);
	CHECK(host.roster.players[1].gear_mode == 0 &&
		clients[0].config.profile.gear_mode == 1);
	memset(events, 0, sizeof(events));
	CHECK(key_frame(&clients[0], &client_keys, events, 1111, &output,
		NULL) == MH_ERROR_NONE &&
		!output.count);
	CHECK(key_frame(&clients[0], &client_keys, events, 1111.25, &output,
		NULL) == MH_ERROR_NONE &&
		output.count == 1);
	pump(1112);
	CHECK(host.roster.players[1].gear_mode == 1 &&
		clients[1].roster.players[1].gear_mode == 1);
	events[0x1d] = events[0x2f] = 1;
	CHECK(key_frame(&clients[0], &client_keys, events, 1113, &output,
		NULL) == MH_ERROR_NONE &&
		!output.count &&
		!strcmp((char *)client_keys.draft, "Clipboard"));

	/* A later action cannot commit an earlier send if capacity runs out. */
	setup();
	join(0, 0);
	join(1, 10);
	memset(&host_keys, 0, sizeof(host_keys));
	memset(events, 0, sizeof(events));
	events[0x3b] = events[0x3d] = 1;
	CHECK(key_frame(&host, &host_keys, events, 100, &output, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < MH_NET_OUTBOX / 2 - 1; i++)
		CHECK(mh_net_session_chat(&host, "queued", 0, NULL) ==
			MH_ERROR_NONE);

	saved = host;
	before = host_keys;
	output_before = output;
	memset(events, 0, sizeof(events));
	CHECK(key_frame(&host, &host_keys, events, 601, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(!memcmp(&saved, &host, sizeof(host)) &&
		!memcmp(&before, &host_keys, sizeof(host_keys)) &&
		!memcmp(&output_before, &output, sizeof(output)));
	drain(&host);
	CHECK(key_frame(&host, &host_keys, events, 601, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 2);
	pump(601);
	CHECK(clients[0].roster.laps == 5);

	/* The complete maximum fan-out must fit, with every original packet. */
	setup();

	for (i = 0; i < 7; i++)
		join(i, i * 10);

	memset(&host_keys, 0, sizeof(host_keys));
	memset(events, 0, sizeof(events));

	for (i = 0; i < 8; i++)
		events[0x3b + i] = 1;

	CHECK(key_frame(&host, &host_keys, events, (double)MH_U32_MAX, &output,
		NULL) == MH_ERROR_NONE &&
		output.count == 8 && host.out_count == 49);
	CHECK(host_keys.confirm_clear == 1 && host.phase == MH_NET_LOADING);
	CHECK(output.actions[0].values.gear == 0 &&
		output.actions[1].values.gear == 1);
	CHECK(output.actions[3].values.laps == 1 &&
		output.actions[4].values.laps == 3);
	pump((double)MH_U32_MAX);

	for (i = 0; i < 7; i++) {
		CHECK(clients[i].phase == MH_NET_LOADING &&
			clients[i].roster.mode == 1 &&
			clients[i].roster.laps == 3);
		CHECK(!strcmp(clients[i].roster.track, "Beta Track") &&
			!strcmp(clients[i].roster.players[0].car, "Beta Car"));
	}
}

static void
return_acknowledgement(void)
{
	struct mh_net_message message;
	struct mh_lobby_keys keys;
	struct mh_lobby_key_output out;
	unsigned char events[MH_KEY_COUNT];
	unsigned int i;

	setup();

	for (i = 0; i < 3; i++)
		join(i, i * 10);

	CHECK(mh_net_session_begin_race(&host, 100, NULL) == MH_ERROR_NONE);
	pump(100);
	CHECK(service_frame(&host, 1001, NULL) == MH_ERROR_NONE);
	drain(&host);
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE);
	pump(1002);
	CHECK(host.return_pending == 14);
	memset(&keys, 0, sizeof(keys));
	memset(events, 0, sizeof(events));
	events[0x3f] = 1;
	CHECK(key_frame(&host, &keys, events, 1002, &out, NULL) ==
			MH_ERROR_NONE &&
		out.count == 1 && out.actions[0].kind == MH_LOBBY_KEY_WAIT);
	CHECK(host.phase == MH_NET_LOBBY && host.out_count == 0);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LOBBY_PONG;
	message.player = 1;
	message.stamp = 1001;
	CHECK(deliver(&host, 2, &message, 0, 1002, NULL) == MH_ERROR_NONE &&
		host.return_pending == 14);
	CHECK(service_frame(&host, 1003, NULL) == MH_ERROR_NONE);
	drain(&host);
	CHECK(deliver(&host, 2, &message, 0, 1004, NULL) == MH_ERROR_NONE &&
		host.return_pending == 14);
	message.stamp = 1003;
	message.type = MH_NET_PONG;
	CHECK(deliver(&host, 2, &message, 0, 1004, NULL) == MH_ERROR_NONE &&
		host.return_pending == 14);
	message.type = MH_NET_LOBBY_PONG;
	message.player = 2;
	saved = host;
	CHECK(deliver(&host, 2, &message, 0, 1004, NULL) != MH_ERROR_NONE &&
		!memcmp(&host, &saved, sizeof(host)));
	message.player = 1;
	CHECK(deliver(&host, 2, &message, 0, 1004, NULL) == MH_ERROR_NONE &&
		host.return_pending == 12);
	CHECK(mh_net_session_kick(&host, 2, NULL) == MH_ERROR_NONE &&
		host.return_pending == 8);
	pump(1005);
	CHECK(mh_net_session_disconnected(&host, 4, 1006, NULL) ==
			MH_ERROR_NONE &&
		host.return_pending == 0);
	pump(1006);
	CHECK(key_frame(&host, &keys, events, 1007, &out, NULL) ==
			MH_ERROR_NONE &&
		out.actions[0].kind == MH_LOBBY_KEY_START);
	CHECK(host.phase == MH_NET_LOADING);
	pump(1007);

	setup();
	join(0, 0);
	CHECK(mh_net_session_begin_race(&host, 2000, NULL) == MH_ERROR_NONE);
	pump(2000);
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE &&
		host.return_pending == 2);
	drain(&host);
	CHECK(service_frame(&host, 122001, NULL) == MH_ERROR_NONE &&
		host.return_pending == 0 && host.roster.players[1].flags == 0);
}

static void
lobby_status_feed(void)
{
	struct mh_lobby_roster view, before;
	struct mh_lobby_roster_view layout;
	struct mh_net_message message;
	struct mh_net_profile profile;
	unsigned int advance[256], i, seen;
	unsigned char palette[768];
	unsigned long revision;

	setup();
	join(0, 0);
	join(1, 10);
	revision = clients[0].replication.status_revision;
	CHECK(service_frame(&host, 1000, NULL) == MH_ERROR_NONE &&
		host.out_count == 0);
	CHECK(service_frame(&host, 1000.25, NULL) == MH_ERROR_NONE);
	pump(1008);
	CHECK(clients[0].replication.status_revision == revision + 1 &&
		!clients[0].replication.configured);
	CHECK(service_frame(&host, 2001, NULL) == MH_ERROR_NONE);
	pump(2002);
	CHECK(mh_lobby_session_roster(&clients[0], "Motorhead", 0, &view,
		NULL) == MH_ERROR_NONE);
	CHECK(view.layout == MH_LOBBY_ROSTER_F12);
	CHECK(view.players[2].ping == 7 && view.players[1].lobby &&
		!view.players[1].racing);

	/*
	 * Host drawing sees a newly measured pong before the next status send.
	 */
	CHECK(mh_lobby_session_roster(&host, "Motorhead", 0, &view, NULL) ==
			MH_ERROR_NONE &&
		view.players[2].ping == 1);
	before = view;
	CHECK(mh_lobby_session_roster(&host, "01234567890123456x", 0, &view,
		NULL) != MH_ERROR_NONE &&
		!memcmp(&view, &before, sizeof(view)));

	CHECK(mh_net_session_begin_race(&host, 3000, NULL) == MH_ERROR_NONE);
	pump(3000);
	configure_replication(&host);
	configure_replication(&clients[0]);
	configure_replication(&clients[1]);
	CHECK(mh_net_session_loaded(&host, 3001, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(&clients[0], 3001, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(&clients[1], 3001, NULL) == MH_ERROR_NONE);
	pump(3001);
	CHECK(mh_net_session_return_lobby(&clients[0], NULL) == MH_ERROR_NONE);
	pump(4000);
	join(2, 4001); /* Late admission has no configured car replication. */
	CHECK(mh_net_session_status(&host, 0, 2, 7, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_status(&host, 2, 1, 9, NULL) == MH_ERROR_NONE);
	CHECK(service_frame(&host, 4002, NULL) == MH_ERROR_NONE);
	pump(4003);
	CHECK(!clients[0].replication.configured &&
		!clients[2].replication.configured);
	CHECK(mh_lobby_session_roster(&clients[2], "Motorhead", 0, &view,
		NULL) == MH_ERROR_NONE);
	CHECK(view.players[0].racing && view.players[0].position == 2 &&
		view.players[0].lap == 7);
	CHECK(view.players[2].racing && view.players[2].position == 1 &&
		view.players[2].lap == 9);
	CHECK(view.players[1].lobby && view.players[3].lobby);

	for (i = 0; i < 256; i++)
		advance[i] = 6;

	memset(palette, 0, sizeof(palette));
	CHECK(mh_lobby_roster_view(&view, advance, palette, &layout, NULL) ==
		MH_ERROR_NONE);

	/* Actual connected session draws rank one first and the host second. */
	seen = 0;

	for (i = 0; i < layout.count; i++)
		if (layout.lines[i].x == 75) {
			if (layout.lines[i].y == 170) {
				CHECK(!strcmp((char *)layout.lines[i].text,
					"Driver2"));
				seen |= 1;
			}

			if (layout.lines[i].y == 182) {
				CHECK(!strcmp((char *)layout.lines[i].text,
					"Driver0"));
				seen |= 2;
			}
		}

	CHECK(seen == 3);

	/*
	 * Unsent local car edits and authoritative membership outrank old data.
	 */
	strcpy(clients[2].config.profile.car, "Local edit");
	clients[2].config.profile.flags |= 4;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_STATUS;
	memcpy(message.status, host.replication.status,
		sizeof(host.replication.status));
	message.status[3].flags = 1;
	message.status[3].value_a = 8;
	CHECK(deliver(&clients[2], 1, &message, 0, 4004, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_lobby_session_roster(&clients[2], "Motorhead", 0, &view,
		NULL) == MH_ERROR_NONE);
	CHECK(view.players[3].lobby && !view.players[3].racing &&
		view.players[3].recording &&
		!strcmp(view.players[3].car, "Local edit"));
	CHECK(mh_net_session_leave(&clients[0], NULL) == MH_ERROR_NONE);
	pump(4005);
	CHECK(!(clients[2].replication.status_valid & 2));
	message.status[1].flags = 1;
	CHECK(deliver(&clients[2], 1, &message, 0, 4006, NULL) ==
		MH_ERROR_NONE);
	CHECK(!(clients[2].replication.status_valid & 2));
	CHECK(mh_lobby_session_roster(&clients[2], "Motorhead", 0, &view,
		NULL) == MH_ERROR_NONE);
	CHECK(!view.players[1].racing && !view.players[1].lobby);
	profile = clients[2].config.profile;
	strcpy(profile.name, "Changed");
	CHECK(mh_net_session_profile(&clients[2], &profile, NULL) ==
		MH_ERROR_NONE);
	pump(4007);
	CHECK(clients[2].replication.status[3].value_a ==
		clients[2].roster.players[3].value_a);

	setup();
	join(0, 0);
	join(1, 0);

	for (i = 0; i < MH_NET_OUTBOX / 2 - 1; i++)
		CHECK(mh_net_session_chat(&host, "fill", 0, NULL) ==
			MH_ERROR_NONE);

	saved = host; /* Pings fit; the following status fan-out does not. */
	CHECK(service_frame(&host, 1001, NULL) != MH_ERROR_NONE &&
		!memcmp(&host, &saved, sizeof(host)));
	setup();
	CHECK(mh_net_session_connected(&clients[0], 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(deliver(&clients[0], 1, &message, 0, 1, NULL) == MH_ERROR_NONE &&
		!clients[0].replication.status_valid);
}

static void
admission_cars(void)
{
	static const char *const names[4] = {
		"First", "Kobra.car", "Third", "Locked"};

	struct mh_net_profile profile;
	struct mh_net_message message;
	unsigned int i;

	setup();
	host.config.car_count = 3;
	CHECK(mh_net_session_cars(&host, names, 4, NULL) == MH_ERROR_NONE);
	strcpy(clients[0].config.profile.car, "kObRa.CaR");
	strcpy(clients[1].config.profile.car, "Locked");
	strcpy(clients[2].config.profile.car, "Unknown");

	for (i = 0; i < 3; i++)
		join(i, 0);

	CHECK(!strcmp(host.lobby.slots[1].profile.car, "kObRa.CaR"));
	CHECK(!strcmp(clients[0].config.profile.car, "kObRa.CaR"));
	CHECK(!strcmp(host.lobby.slots[2].profile.car, "First"));
	CHECK(!strcmp(clients[1].config.profile.car, "First"));
	CHECK(!strcmp(host.lobby.slots[3].profile.car, "First"));
	CHECK(!strcmp(clients[2].config.profile.car, "First"));
	saved = host;
	CHECK(mh_net_session_cars(&host, names, 4, NULL) != MH_ERROR_NONE &&
		!memcmp(&host, &saved, sizeof(host)));

	/* Type 5 does not repeat the original admission lookup. */
	profile = clients[1].config.profile;
	strcpy(profile.car, "Locked");
	CHECK(mh_net_session_profile(&clients[1], &profile, NULL) ==
		MH_ERROR_NONE);
	pump(1);
	CHECK(!strcmp(host.lobby.slots[2].profile.car, "Locked"));

	/*
	 * An older periodic roster cannot overwrite a newer unsent local edit.
	 */
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_ROSTER;
	message.roster = host.roster;
	strcpy(clients[1].config.profile.car, "Third");
	CHECK(deliver(&clients[1], 1, &message, 1, 1, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(clients[1].config.profile.car, "Third"));
	CHECK(mh_net_session_begin_race(&host, 2, NULL) == MH_ERROR_NONE);
	pump(2);
	strcpy(clients[3].config.profile.car, "Locked");
	join(3, 3);
	CHECK(!strcmp(host.lobby.slots[4].profile.car, "First"));
	CHECK(!strcmp(clients[3].config.profile.car, "First"));

	/*
	 * A queue failure after lookup must restore the original reservation.
	 */
	setup();
	host.config.car_count = 3;
	CHECK(mh_net_session_cars(&host, names, 4, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_connected(&host, 2, 0, NULL) == MH_ERROR_NONE);
	host.lobby.slots[1].peer = 2;
	host.lobby.slots[1].phase = MH_NET_RESERVED;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PROFILE_JOIN;
	message.player = 1;
	message.profile = clients[0].config.profile;
	strcpy(message.profile.car, "Locked");
	host.out_count = MH_NET_OUTBOX;
	saved = host;
	CHECK(deliver(&host, 2, &message, 1, 1, NULL) != MH_ERROR_NONE &&
		!memcmp(&host, &saved, sizeof(host)));
}

static void
patched_init(void)
{
	struct mh_net_session_config settings;
	unsigned int i;

	settings = session_config(0);
	settings.wire = MH_NET_WIRE_31_LOBBY;
	settings.provider = MH_NET_PROVIDER_LANINET;
	CHECK(mh_net_session_init(1, &settings, &host, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		settings = session_config(i + 1);
		settings.wire = MH_NET_WIRE_31_LOBBY;
		settings.provider = MH_NET_PROVIDER_LANINET;
		CHECK(mh_net_session_init(0, &settings, &clients[i], NULL) ==
			MH_ERROR_NONE);
	}
}

static void
patched_profiles(void)
{
	struct mh_net_profile profile;
	const struct mh_net_outgoing *out;
	unsigned char packet[88];

	patched_init();

	join(0, 0);
	join(1, 1);
	profile = clients[0].config.profile;
	strcpy(profile.name, "New Driver");
	profile.flags = MH_NET_FLAG_RECORDING;
	profile.control_type = 3;
	profile.values[MH_NET_PROFILE_RACES] = 123;
	profile.values[MH_NET_PROFILE_POINTS] = 456;
	profile.value_a = 37;
	profile.value_b = 99;
	CHECK(mh_net_session_profile(&clients[0], &profile, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(clients[0].roster.players[1].name, "New Driver") == 0);
	CHECK(clients[0].roster.players[1].flags == 6 &&
		clients[0].roster.players[1].value_a == 37);
	out = mh_net_session_outgoing(&clients[0]);
	CHECK(out != NULL && out->size == sizeof(packet) && out->peer == 1 &&
		out->reliable);

	if (out == NULL)
		return;

	memcpy(packet, out->data, sizeof(packet));
	CHECK(packet[0] == 5 && packet[2] == 88 && packet[4] == 1 &&
		packet[5] == 6 && packet[85] == 1 && packet[86] == 1 &&
		packet[87] == 3);
	mh_net_session_sent(&clients[0]);
	CHECK(mh_net_session_receive(&host, 2, packet, sizeof(packet), 1, 2,
		NULL) == MH_ERROR_NONE);
	out = mh_net_session_outgoing(&host);
	CHECK(host.out_count == 1 && out != NULL && out->peer == 3 &&
		out->size == sizeof(packet));

	if (out != NULL)
		CHECK(out->reliable);

	CHECK(host.roster.players[1].value_a == 0 &&
		host.roster.players[1].value_b == 0);
	CHECK(host.roster.players[1].values[MH_NET_PROFILE_RACES] == 0 &&
		host.roster.players[1].values[MH_NET_PROFILE_POINTS] == 0);
	pump(2);
	CHECK(strcmp(clients[1].roster.players[1].name, "New Driver") == 0);
	CHECK(clients[1].roster.players[1].control_type == 3 &&
		clients[1].roster.players[1].flags == 6);
	CHECK(clients[1].roster.players[1].values[MH_NET_PROFILE_RACES] == 0 &&
		clients[1].roster.players[1].values[MH_NET_PROFILE_POINTS] ==
			0);

	/* The sender's local state does not rely on an echoed roster. */
	CHECK(clients[0].roster.players[1].value_a == 37);

	profile = host.config.profile;
	strcpy(profile.name, "New Host");
	profile.value_a = 83;
	profile.value_b = 98;
	CHECK(mh_net_session_profile(&host, &profile, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 2 && host.roster.players[0].value_a == 0);
	out = mh_net_session_outgoing(&host);
	CHECK(out != NULL && out->size == 88 && out->data[0] == 5 &&
		out->data[4] == 0 && out->data[85] == 1 && out->data[86] == 1);
	pump(3);
	CHECK(strcmp(clients[0].roster.players[0].name, "New Host") == 0);
	CHECK(strcmp(clients[1].roster.players[0].name, "New Host") == 0);
	CHECK(clients[0].roster.players[0].value_a == 1);

	/*
	 * Keep native sender binding and atomicity when forwarding cannot
	 * queue.
	 */
	packet[4] = 2;
	saved = host;
	CHECK(mh_net_session_receive(&host, 2, packet, sizeof(packet), 1, 4,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&host, &saved, sizeof(host)) == 0);
	packet[4] = 1;
	host.out_count = MH_NET_OUTBOX;
	saved = host;
	CHECK(mh_net_session_receive(&host, 2, packet, sizeof(packet), 1, 4,
		NULL) == MH_ERROR_RANGE);
	CHECK(memcmp(&host, &saved, sizeof(host)) == 0);
	clients[0].out_count = MH_NET_OUTBOX;
	saved = clients[0];
	CHECK(mh_net_session_profile(&clients[0], &profile, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(&clients[0], &saved, sizeof(saved)) == 0);
}

static void
patched_message(struct mh_net_session *session, unsigned long peer,
	const struct mh_net_message *message, double now_ms)
{
	unsigned char data[MH_NET_WIRE_PACKET_LIMIT];
	size_t size;

	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_receive(session, peer, data, size,
		message->type == MH_NET_CHAT ||
			message->type == MH_NET_START ||
			message->type == MH_NET_ROSTER ||
			message->type == MH_NET_LEAVE_RACE,
		now_ms, NULL) == MH_ERROR_NONE);
}

/* Isolate readiness with a frozen loading roster. */
static void
patched_loading(unsigned int count)
{
	unsigned int i;

	patched_init();

	for (i = 0; i < count; i++)
		join(i, 0);

	host.phase = MH_NET_LOADING;

	for (i = 0; i <= count; i++) {
		host.lobby.slots[i].phase = MH_NET_IN_RACE;
		host.lobby.slots[i].profile.flags = MH_NET_FLAG_RACING;
		host.roster.players[i].flags = MH_NET_FLAG_RACING;
	}

	for (i = 0; i < count; i++) {
		clients[i].roster = host.roster;
		clients[i].phase = MH_NET_LOADING;
	}
}

static void
patched_score_delivery(void)
{
	struct mh_lobby_roster view;
	unsigned int i, positions[MH_NET_PLAYERS];
	static const unsigned int points[4] = {6, 8, 10, 12};

	patched_loading(3);
	configure_replication(&host);
	host.phase = MH_NET_RACING;
	memset(positions, 0, sizeof(positions));

	for (i = 0; i < 4; i++) {
		positions[i] = 4 - i;
		CHECK(mh_net_session_status(&host, i, positions[i], 2 + i,
			NULL) == MH_ERROR_NONE);
		host.lobby.slots[i].profile.values[MH_NET_PROFILE_RACES] =
			MH_U32_MAX;
		host.lobby.slots[i].profile.values[MH_NET_PROFILE_POINTS] =
			MH_U32_MAX;
	}

	host.out_count = MH_NET_OUTBOX - 2;
	saved = host;
	CHECK(mh_net_session_finish_race(&host, positions, NULL) ==
		MH_ERROR_RANGE);
	CHECK(!memcmp(&host, &saved, sizeof(saved)));
	host.out_count = 0;
	CHECK(mh_net_session_finish_race(&host, positions, NULL) ==
		MH_ERROR_NONE);
	CHECK(host.out_count == 3 && host.return_pending == 14);
	saved = host;
	CHECK(mh_net_session_finish_race(&host, positions, NULL) ==
		MH_ERROR_INVALID);
	CHECK(!memcmp(&host, &saved, sizeof(saved)));
	pump(100);
	CHECK(host.return_pending == 14);
	CHECK(mh_net_session_tick(&host, 1001, NULL) == MH_ERROR_NONE);
	pump(1001);
	CHECK(host.return_pending == 0);

	for (i = 0; i < 4; i++) {
		CHECK(host.roster.players[i].value_a == positions[i] &&
			host.roster.players[i].value_b == 2 + i);
		CHECK(host.roster.players[i].values[MH_NET_PROFILE_RACES] == 0);
		CHECK(host.roster.players[i].values[MH_NET_PROFILE_POINTS] ==
			points[i] - 1);

		if (i == 0)
			continue;

		CHECK(!memcmp(&host.roster, &clients[i - 1].roster,
			sizeof(host.roster)));
		CHECK(clients[i - 1]
				.config.profile.values[MH_NET_PROFILE_RACES] ==
			0);
		CHECK(clients[i - 1]
				.config.profile.values[MH_NET_PROFILE_POINTS] ==
			points[i] - 1);
	}

	/*
	 * An ordinary profile edit after return must retain the awarded score.
	 */
	CHECK(mh_net_session_profile(&clients[0], &clients[0].config.profile,
		NULL) == MH_ERROR_NONE);
	pump(1002);
	CHECK(host.roster.players[1].values[MH_NET_PROFILE_POINTS] == 7);
	CHECK(host.roster.players[1].value_a == 3 &&
		host.roster.players[1].value_b == 3);
	CHECK(host.replication.status[1].value_a == 3 &&
		host.replication.status[1].value_b == 3);
	CHECK(mh_lobby_session_roster(&host, "Progress", 0, &view, NULL) ==
		MH_ERROR_NONE);
	CHECK(view.players[1].position == 3 && view.players[1].lap == 3);
	CHECK(mh_net_session_tick(&host, 2002, NULL) == MH_ERROR_NONE);
	pump(2002);
	CHECK(clients[0].replication.status[1].value_a == 3 &&
		clients[0].replication.status[1].value_b == 3);

	/*
	 * New setup uses fresh grid/lap values before loading resets progress.
	 */
	CHECK(mh_net_session_begin_race(&host, 2003, NULL) == MH_ERROR_NONE);
	pump(2003);

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		CHECK(host.roster.players[i].value_a ==
			host.lobby.slots[i].profile.value_a);
		CHECK(host.roster.players[i].value_b == 1);
		CHECK(clients[0].roster.players[i].value_a ==
			host.roster.players[i].value_a);
		CHECK(clients[0].roster.players[i].value_b == 1);
	}

	CHECK(host.progress[1].position == 3 && host.progress[1].lap == 3);
	configure_replication(&host);

	for (i = 0; i < MH_NET_PLAYERS; i++)
		CHECK(host.progress[i].position == 0 &&
			host.progress[i].lap == 1);
}

static void
patched_setup(void)
{
	struct mh_net_message message;
	struct mh_lobby_keys keys;
	struct mh_lobby_key_output actions;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	const struct mh_net_outgoing *out;
	unsigned char events[MH_KEY_COUNT];
	unsigned int mode, i, seen;

	for (mode = 0; mode <= MH_NET_MODE_ELIMINATION; mode++) {
		patched_init();
		host.config.mode = mode;
		host.config.laps = 9;

		for (i = 0; i < 3; i++)
			join(i, 0);

		memset(&keys, 0, sizeof(keys));
		memset(events, 0, sizeof(events));
		events[0x3f] = 1;
		CHECK(key_frame(&host, &keys, events, 1, &actions, NULL) ==
			MH_ERROR_NONE);
		CHECK(host.phase == MH_NET_LOADING && host.out_count == 3);
		pump(1);

		for (i = 0; i < 3; i++)
			CHECK(clients[i].phase == MH_NET_LOADING &&
				clients[i].roster.mode == mode &&
				clients[i].roster.laps == 9);

		patched_init();
		host.config.mode = mode;
		host.config.laps = 9;

		for (i = 0; i < 3; i++)
			join(i, 0);

		CHECK(mh_net_session_leave(&clients[1], NULL) == MH_ERROR_NONE);
		pump(20);

		/* An unadmitted transport peer is not a race participant. */
		CHECK(mh_net_session_connected(&host, 9, 20, NULL) ==
			MH_ERROR_NONE);
		host.return_pending = 128;
		host.connections[0].contact_ms = 51;
		host.connections[2].contact_ms = 53;
		clients[0].contact_ms = 40;
		clients[2].contact_ms = 42;
		host.replication.history[1].received_ms = 17;
		clients[0].replication.history[0].received_ms = 19;
		host.replication.status[1].ping_ms = 23;
		host.race_service_ms = 37;
		host.ready_mask = 255;
		host.out_count = MH_NET_OUTBOX - 1;
		saved = host;
		CHECK(mh_net_session_begin_race(&host, 100, NULL) ==
			MH_ERROR_RANGE);
		CHECK(!memcmp(&host, &saved, sizeof(saved)));
		host.out_count = 0;
		CHECK(mh_net_session_begin_race(&host, 100, NULL) ==
			MH_ERROR_NONE);
		CHECK(host.out_count == 2 && host.phase == MH_NET_LOADING);
		out = mh_net_session_outgoing(&host);
		CHECK(out != NULL && out->reliable && out->size == 1103);

		if (out == NULL)
			return;

		CHECK(mh_net_wire_begin(host.config.wire, out->data, out->size,
			&reader, NULL) == MH_ERROR_NONE);
		CHECK(mh_net_wire_next(host.config.wire, &reader, &frame) == 1);
		CHECK(mh_net_wire_decode(host.config.wire, &frame, &message,
			NULL) == MH_ERROR_NONE);
		CHECK(message.type == MH_NET_RACE_CONFIG &&
			message.roster.mode == mode &&
			message.roster.laps == 9);
		seen = 0;

		for (i = 0; i < MH_NET_PLAYERS; i++) {
			if (i != 0 && i != 1 && i != 3) {
				CHECK(message.roster.players[i].flags == 0);
				continue;
			}

			CHECK(message.roster.players[i].flags ==
				MH_NET_FLAG_LOBBY);
			CHECK(message.roster.players[i].value_a >= 1 &&
				message.roster.players[i].value_a <= 3);
			seen |= 1U << message.roster.players[i].value_a;
			CHECK(message.roster.players[i].value_b == 1);
			CHECK(host.roster.players[i].flags ==
				MH_NET_FLAG_RACING);
		}

		CHECK(seen == 14);
		saved = clients[0];
		CHECK(mh_net_session_receive(&clients[0], 1, out->data,
			out->size, 0, 100, NULL) != MH_ERROR_NONE);
		CHECK(!memcmp(&clients[0], &saved, sizeof(saved)));
		pump(100);
		CHECK(clients[0].phase == MH_NET_LOADING &&
			clients[2].phase == MH_NET_LOADING);
		CHECK(!memcmp(
			&host.roster, &clients[0].roster, sizeof(host.roster)));
		CHECK(!memcmp(
			&host.roster, &clients[2].roster, sizeof(host.roster)));
		CHECK(host.connections[0].contact_ms == 51 &&
			host.connections[2].contact_ms == 53);
		CHECK(clients[0].contact_ms == 40 &&
			clients[2].contact_ms == 42);
		CHECK(host.replication.history[1].received_ms == 17 &&
			clients[0].replication.history[0].received_ms == 19);
		CHECK(host.replication.status[1].ping_ms == 23 &&
			host.race_service_ms == 37 && host.ready_mask == 0);
	}
}

static void
patched_readiness(void)
{
	struct mh_net_message message;
	const struct mh_net_outgoing *out;
	unsigned char data[9];
	size_t size;

	patched_init();
	join(0, 0);
	join(1, 0);
	CHECK(mh_net_session_begin_race(&host, 0, NULL) == MH_ERROR_NONE);
	pump(0);
	configure_replication(&host);
	configure_replication(&clients[0]);
	configure_replication(&clients[1]);
	host.ready_mask = 7;
	CHECK(mh_net_session_loaded(&host, 0, NULL) == MH_ERROR_NONE);
	CHECK(host.ready_mask == 1);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PING;
	message.stamp = 1000;
	patched_message(&clients[0], 1, &message, 1042.75);
	out = mh_net_session_outgoing(&clients[0]);
	CHECK(out != NULL && out->data[0] == MH_NET_LOBBY_PONG);
	pump(1100);
	CHECK(host.ready_mask == 1);
	CHECK(mh_net_session_loaded(&clients[0], 1100, NULL) == MH_ERROR_NONE);
	CHECK(clients[0].ready_mask == 0);
	patched_message(&clients[0], 1, &message, 1200);
	CHECK(clients[0].out_count == 0);
	CHECK(clients[0].replication.pending_service[0].size == 9);
	CHECK(mh_net_session_tick(&clients[0], 1230, NULL) == MH_ERROR_NONE);
	out = mh_net_session_outgoing(&clients[0]);
	CHECK(out != NULL && out->size == 49 && !out->reliable &&
		out->data[0] == MH_NET_PONG && out->data[4] == 1 &&
		out->data[5] == 0xe8 && out->data[6] == 3);

	/* No outstanding ping is required; the echoed stamp is sufficient. */
	host.connections[0].ping_pending = 0;
	host.connections[0].ping_stamp = 1199;
	pump(1250.75);
	CHECK(host.ready_mask == 3 && host.connections[0].ping_ms == 250 &&
		host.connections[0].contact_ms == 1250);
	CHECK(mh_net_session_start_frame(&host, 1300, NULL) == MH_ERROR_NONE);
	CHECK(host.start.anchor_ms == 0); /* Second client still loading. */
	CHECK(mh_net_session_loaded(&clients[1], 1300, NULL) == MH_ERROR_NONE);
	patched_message(&clients[1], 1, &message, 1400);
	CHECK(mh_net_session_tick(&clients[1], 1450, NULL) == MH_ERROR_NONE);
	pump(1500);
	CHECK(host.ready_mask == 7);
	CHECK(mh_net_session_start_frame(&host, 1501.75, NULL) ==
		MH_ERROR_NONE);
	CHECK(host.start.anchor_ms == 1501);
	CHECK(mh_net_session_start_frame(&host, 3251.25, NULL) ==
		MH_ERROR_NONE);
	out = mh_net_session_outgoing(&host);
	CHECK(out != NULL && out->peer == 3 && out->reliable &&
		out->size == 4 && out->data[0] == MH_NET_START);
	CHECK(host.phase == MH_NET_RACING && host.start.started_ms == 3251);
	pump(3300);
	CHECK(clients[1].phase == MH_NET_RACING &&
		clients[1].start.started_ms == 3300);
	CHECK(clients[0].phase == MH_NET_WAITING);

	/* A later delivered START also samples the next game frame. */
	message.type = MH_NET_START;
	message.player = 0;
	patched_message(&clients[1], 1, &message, 9000);
	CHECK(clients[1].start.started_ms == 3300 && clients[1].start_pending);
	CHECK(mh_net_session_start_frame(&clients[1], 3400.75, NULL) ==
		MH_ERROR_NONE);
	CHECK(clients[1].start.started_ms == 3400 && !clients[1].start_pending);
	clients[1].phase = MH_NET_LOBBY;
	patched_message(&clients[1], 1, &message, 9001);
	CHECK(!clients[1].start_pending);

	/* Rejected early or unreliable START must leave the session intact. */
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	clients[0].phase = MH_NET_LOADING;
	saved = clients[0];
	CHECK(mh_net_session_receive(&clients[0], 1, data, size, 1, 9002,
		NULL) == MH_ERROR_STATE);
	CHECK(!memcmp(&saved, &clients[0], sizeof(saved)));
	CHECK(mh_net_session_receive(&clients[0], 1, data, size, 0, 9002,
		NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&saved, &clients[0], sizeof(saved)));

	message.type = MH_NET_PONG;
	message.player = 2;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	saved = host;
	CHECK(mh_net_session_receive(&host, 2, data, size, 0, 9003, NULL) !=
		MH_ERROR_NONE);

	/* Sender cannot claim another slot. */
	CHECK(!memcmp(&saved, &host, sizeof(saved)));
}

static void
patched_receipt(void)
{
	struct mh_net_message message;
	struct mh_net_vehicle vehicle;
	unsigned char packet[80];
	struct mh_net_history *history;
	size_t size;
	unsigned int patched;

	for (patched = 0; patched <= 1; patched++) {
		patched_loading(1);
		host.config.wire =
			patched ? MH_NET_WIRE_31_LOBBY : MH_NET_WIRE_F12;
		configure_replication(&host);
		history = &host.replication.history[1];
		vehicle = vehicle_snapshot(1);
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_CAR;
		message.player = 1;
		message.view_player = 1;
		message.stamp = 100;
		CHECK(mh_net_vehicle_encode(&vehicle, message.car,
			MH_NET_VEHICLE_BYTES, NULL) == MH_ERROR_NONE);
		CHECK(deliver(&host, 2, &message, 0, 1000.75, NULL) ==
			MH_ERROR_NONE);
		CHECK(history->count == 1 && history->received_ms == 1000);
		message.view_player = 0;
		CHECK(deliver(&host, 2, &message, 0, 9000.75, NULL) ==
			MH_ERROR_NONE);
		CHECK(history->count == 1 && host.replication.view[1] == 1);
		CHECK(history->received_ms == (patched ? 9000 : 1000));
		message.stamp = 99;
		CHECK(deliver(&host, 2, &message, 0, 9100.75, NULL) ==
			MH_ERROR_NONE);
		CHECK(history->count == 1 && history->entries[0].stamp == 100);
		CHECK(history->received_ms == (patched ? 9100 : 1000));

		/*
		 * A malformed later frame cannot refresh the preceding
		 * snapshot.
		 */
		CHECK(mh_net_wire_encode(host.config.wire, &message, packet,
			sizeof(packet), &size, NULL) == MH_ERROR_NONE);
		saved = host;
		memcpy(packet + size, packet, size);
		packet[size + 2]--;
		CHECK(mh_net_session_receive(&host, 2, packet, size * 2, 0,
			9200, NULL) != MH_ERROR_NONE);
		CHECK(!memcmp(&host, &saved, sizeof(host)));

		/*
		 * Slot ownership and host-only detailed input remain enforced.
		 */
		message.player = 0;
		CHECK(deliver(&host, 2, &message, 0, 9200, NULL) !=
			MH_ERROR_NONE);
		CHECK(!memcmp(&host, &saved, sizeof(host)));
		message.player = 1;
		message.type = MH_NET_CAR_FAR;
		CHECK(deliver(&host, 2, &message, 0, 9200, NULL) !=
			MH_ERROR_NONE);
		CHECK(!memcmp(&host, &saved, sizeof(host)));
	}
}

static void
patched_liveness(void)
{
	struct mh_net_message message;
	const struct mh_net_outgoing *out;
	unsigned int i;

	patched_init();
	join(0, 0);
	join(1, 0);

	/*
	 * Per-peer history must not stagger the shared original lobby cadence.
	 */
	host.connections[0].ping_sent_ms = 10000;
	CHECK(mh_net_session_tick(&host, 1000, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 0);
	CHECK(mh_net_session_tick(&host, 1000.5, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 4 && host.replication.status_ms == 1000);

	for (i = 0; i < 2; i++) {
		out = mh_net_session_outgoing(&host);
		CHECK(out != NULL && out->size == 8 && !out->reliable &&
			out->data[0] == 10 && out->data[4] == 0xe8 &&
			out->data[5] == 3);
		mh_net_session_sent(&host);
	}

	out = mh_net_session_outgoing(&host);
	CHECK(out != NULL && out->data[0] == 15 && out->size == 95);
	drain(&host);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PING;
	message.stamp = 1000;
	patched_message(&clients[0], 1, &message, 1042.75);
	out = mh_net_session_outgoing(&clients[0]);
	CHECK(out != NULL && out->size == 9 && !out->reliable &&
		out->data[0] == 11 && out->data[4] == 1 &&
		out->data[5] == 0xe8 && out->data[6] == 3);
	CHECK(clients[0].contact_ms == 1042);
	drain(&clients[0]);
	message.type = MH_NET_LOBBY_PONG;
	message.player = 1;
	patched_message(&host, 2, &message, 1100);
	CHECK(host.connections[0].ping_ms == 100 &&
		host.connections[0].contact_ms == 1100);

	/* Delayed replies still measure their own echoed timestamp. */
	message.stamp = 999;
	patched_message(&host, 2, &message, 1250.75);
	CHECK(host.connections[0].ping_ms == 251 &&
		host.connections[0].contact_ms == 1250);

	/* Ordinary traffic cannot mask missing ping/pong liveness. */
	message.type = MH_NET_CHAT;
	strcpy(message.text, "still typing");
	patched_message(&host, 2, &message, 59000);
	CHECK(host.connections[0].contact_ms == 1250);
	host.connections[1].contact_ms = 59000;
	drain(&host);
	CHECK(mh_net_session_tick(&host, 61250, NULL) == MH_ERROR_NONE);
	CHECK(!host.connections[0].closing);
	drain(&host);
	CHECK(mh_net_session_tick(&host, 61250.5, NULL) == MH_ERROR_NONE);
	CHECK(host.connections[0].closing && !host.connections[1].closing);
	CHECK(host.lobby.slots[1].phase == MH_NET_EMPTY);
	message.player = 0;
	patched_message(&clients[0], 1, &message, 60000);
	CHECK(clients[0].contact_ms == 1042);
	CHECK(mh_net_session_tick(&clients[0], 61042, NULL) == MH_ERROR_NONE);
	CHECK(clients[0].phase == MH_NET_LOBBY);
	CHECK(mh_net_session_tick(&clients[0], 61042.5, NULL) == MH_ERROR_NONE);
	CHECK(clients[0].phase == MH_NET_CLOSED);
}

static void
patched_race_batches(void)
{
	const struct mh_net_outgoing *out;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_message message;
	unsigned int i, near, far, statuses, pings;

	patched_loading(2);
	configure_replication(&host);
	CHECK(mh_net_session_loaded(&host, 0, NULL) == MH_ERROR_NONE);
	host.ready_mask = 7;

	for (i = 0; i < 2; i++) {
		configure_replication(&clients[i]);
		CHECK(mh_net_session_loaded(&clients[i], 0, NULL) ==
			MH_ERROR_NONE);
	}

	CHECK(mh_net_session_tick(&clients[0], 67, NULL) == MH_ERROR_NONE);
	pump(67);

	/*
	 * Presented positions choose one near and one far car for recipient 1.
	 */
	host.replication.position[2][0] = 400;
	CHECK(mh_net_session_tick(&host, 100, NULL) == MH_ERROR_NONE);
	pump(101);
	CHECK(host.replication.history[2].count == 0);
	CHECK(clients[0].replication.history[2].count == 0 &&
		clients[0].replication.history[2].received_ms == 101);
	CHECK(mh_net_session_tick(&clients[1], 167, NULL) == MH_ERROR_NONE);
	pump(167);
	CHECK(host.replication.history[1].count == 1 &&
		host.replication.history[2].count == 1);
	CHECK(mh_net_session_status(&host, 1, 2, 3, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_tick(&host, 1001.75, NULL) == MH_ERROR_NONE);
	out = mh_net_session_outgoing(&host);
	CHECK(out != NULL && out->peer == 2 && !out->reliable &&
		out->size == 159);

	if (out == NULL)
		return;

	CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, out->data, out->size,
		&reader, NULL) == MH_ERROR_NONE);
	near = far = statuses = pings = 0;

	while (mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 1) {
		CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &message,
			NULL) == MH_ERROR_NONE);

		if (message.type == MH_NET_PING) {
			pings++;
			CHECK(message.stamp == 1001 && statuses == 0);
		} else if (message.type == MH_NET_STATUS) {
			statuses++;
			CHECK(frame.size == 95 &&
				message.status[1].value_a == 2 &&
				message.status[1].value_b == 3);
		} else if (message.type == MH_NET_CAR) {
			near++;
			CHECK(message.player == 0 && message.stamp == 1001);
		} else if (message.type == MH_NET_CAR_FAR) {
			far++;
			CHECK(message.player == 2 && message.stamp == 167);
		} else
			CHECK(0);
	}

	CHECK(pings == 1 && statuses == 1 && near == 1 && far == 1);
	pump(1002);
	CHECK(clients[0].replication.status[1].value_a == 2 &&
		clients[0].replication.status[1].value_b == 3);
	CHECK(clients[0].replication.history[0].entries[0].stamp == 1001);
	CHECK(clients[0].replication.history[2].entries[0].stamp == 167);
	CHECK(clients[0].replication.history[1].count == 0);
}

static void
patched_race_service(void)
{
	const struct mh_net_outgoing *out;
	struct mh_net_message message;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	unsigned int i;

	patched_loading(3);
	configure_replication(&host);
	CHECK(mh_net_session_loaded(&host, 0, NULL) == MH_ERROR_NONE);
	host.roster.mode = MH_NET_MODE_ELIMINATION;
	host.replication.eliminated = 4;
	host.roster.players[3].flags = MH_NET_FLAG_LOBBY;
	host.lobby.slots[3].profile.flags = MH_NET_FLAG_LOBBY;
	host.lobby.slots[3].phase = MH_NET_IN_LOBBY;

	for (i = 0; i < 3; i++) {
		clients[i].roster = host.roster;

		if (i < 2)
			configure_replication(&clients[i]);
	}

	CHECK(mh_net_session_loaded(&clients[0], 0, NULL) == MH_ERROR_NONE);
	clients[2].phase = MH_NET_LOBBY;
	CHECK(mh_net_session_tick(&host, 1000, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 0);
	CHECK(mh_net_session_tick(&host, 1000.5, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 0);

	for (i = 1; i <= 3; i++)
		CHECK(host.replication.pending_service[i].size == 103);

	/* Queued status and ping retain their production-time contents. */
	CHECK(mh_net_session_status(&host, 1, 4, 2, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_tick(&host, 1066, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 0);
	host.out_count = MH_NET_OUTBOX;
	saved = host;
	CHECK(mh_net_session_tick(&host, 1066.25, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&host, &saved, sizeof(host)));
	host.out_count = 0;
	CHECK(mh_net_session_tick(&host, 1066.25, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 3);

	for (i = 0; i < host.out_count; i++) {
		out = &host.outgoing[(host.out_head + i) % MH_NET_OUTBOX];
		CHECK(!out->reliable && out->size == 103);
		CHECK(mh_net_wire_begin(host.config.wire, out->data, out->size,
			&reader, NULL) == MH_ERROR_NONE);
		CHECK(mh_net_wire_next(host.config.wire, &reader, &frame) == 1);
		CHECK(mh_net_wire_decode(host.config.wire, &frame, &message,
			NULL) == MH_ERROR_NONE);
		CHECK(message.type == MH_NET_PING && message.stamp == 1000);
		CHECK(mh_net_wire_next(host.config.wire, &reader, &frame) == 1);
		CHECK(mh_net_wire_decode(host.config.wire, &frame, &message,
			NULL) == MH_ERROR_NONE);
		CHECK(message.type == MH_NET_STATUS &&
			message.status[1].value_a == 0);
		CHECK((message.status[2].flags & MH_NET_FLAG_UNAVAILABLE) ==
			(out->peer == 4 ? 0 : MH_NET_FLAG_UNAVAILABLE));
	}

	pump(1067);
	CHECK(host.ready_mask == 1);
	CHECK(mh_net_session_tick(&clients[0], 1067, NULL) == MH_ERROR_NONE);
	pump(1067);
	CHECK(host.ready_mask == 3);
	CHECK(host.connections[0].contact_ms == 1067 &&
		host.connections[1].contact_ms == 1067 &&
		host.connections[2].contact_ms == 1067);
	CHECK(mh_net_session_loaded(&clients[1], 1100, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_tick(&host, 2001, NULL) == MH_ERROR_NONE);
	pump(2002);
	CHECK(mh_net_session_tick(&clients[0], 2002, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_tick(&clients[1], 2002, NULL) == MH_ERROR_NONE);
	pump(2002);
	CHECK(host.ready_mask == 7);
}

static void
patched_client_backpressure(void)
{
	struct mh_net_session *client;
	struct mh_net_message ping;
	const struct mh_net_outgoing *out;

	patched_loading(1);
	client = &clients[0];
	configure_replication(client);
	CHECK(mh_net_session_loaded(client, 0, NULL) == MH_ERROR_NONE);
	memset(&ping, 0, sizeof(ping));
	ping.type = MH_NET_PING;
	ping.stamp = 123;
	patched_message(client, 1, &ping, 100);
	ping.stamp = 45;
	patched_message(client, 1, &ping, 101);
	CHECK(client->out_count == 0 && client->contact_ms == 101);
	CHECK(client->replication.pending_service[0].size == 18);
	client->out_count = MH_NET_OUTBOX;
	saved = *client;
	CHECK(mh_net_session_tick(client, 200, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(client, &saved, sizeof(saved)));
	client->out_count = 0;
	CHECK(mh_net_session_tick(client, 200, NULL) == MH_ERROR_NONE);
	out = mh_net_session_outgoing(client);
	CHECK(out != NULL && out->size == 58);

	if (out != NULL && out->size == 58) {
		CHECK(mh_read_u32le(out->data + 5) == 123);
		CHECK(mh_read_u32le(out->data + 14) == 45);
		CHECK(out->data[18] == MH_NET_CAR);
	}

	CHECK(client->replication.pending_service[0].size == 0);
	CHECK(client->replication.sent_ms[1] == 200);
}

static void
race_receipt_lifetime(void)
{
	struct mh_net_history expected;
	unsigned int patched, i;

	for (patched = 0; patched <= 1; patched++) {
		patched_loading(1);
		host.config.wire =
			patched ? MH_NET_WIRE_31_LOBBY : MH_NET_WIRE_F12;
		host.connections[0].contact_ms = 2999;

		for (i = 0; i < MH_NET_PLAYERS; i++) {
			host.replication.status[i].ping_ms = 77;
			host.replication.history[i].count = 1;
			host.replication.history[i].entries[0].stamp = 2000;
			host.replication.history[i].now_ms = 3000;
			host.replication.history[i].received_ms = 3000;
			host.replication.history[i].sampled_stamp = 2000;
			host.replication.history[i].local_anchor = 2500;
			host.replication.history[i].remote_anchor = 2000;
			host.replication.history[i].playback.ready = 1;
		}

		mh_net_session_reset_replication(&host);
		memset(&expected, 0, sizeof(expected));
		expected.received_ms = patched ? 3000 : 0;

		for (i = 0; i < MH_NET_PLAYERS; i++) {
			CHECK(!memcmp(&host.replication.history[i], &expected,
				sizeof(expected)));
			CHECK(host.replication.status[i].ping_ms ==
				(patched ? 77UL : 0));
		}

		configure_replication(&host);

		for (i = 0; i < MH_NET_PLAYERS; i++) {
			CHECK(!memcmp(&host.replication.history[i], &expected,
				sizeof(expected)));
			CHECK(host.replication.status[i].ping_ms ==
				(patched ? 77UL : 0));
		}

		CHECK(host.connections[0].contact_ms == 2999);
		CHECK(mh_net_session_loaded(&host, 10000, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_net_session_tick(&host, 10000, NULL) == MH_ERROR_NONE);
		CHECK(!!(host.replication.status[1].flags &
			MH_NET_FLAG_UNAVAILABLE) == !patched);
		CHECK(mh_net_session_tick(&host, 11000, NULL) == MH_ERROR_NONE);
		CHECK(!!(host.replication.status[1].flags &
			MH_NET_FLAG_UNAVAILABLE) == !patched);
		CHECK(mh_net_session_tick(&host, 11000.25, NULL) ==
			MH_ERROR_NONE);
		CHECK(host.replication.status[1].flags &
			MH_NET_FLAG_UNAVAILABLE);

		/*
		 * Lobby cleanup releases prediction but retains patched clocks.
		 */
		mh_net_session_remove_replica(&host, 1);
		CHECK(host.replication.history[1].received_ms ==
			expected.received_ms);
		CHECK(host.replication.history[0].received_ms ==
			expected.received_ms);
	}
}

static void
patched_service_lifetime(void)
{
	unsigned int i;

	patched_loading(1);
	configure_replication(&host);
	CHECK(mh_net_session_loaded(&host, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_tick(&host, 1200, NULL) == MH_ERROR_NONE);
	CHECK(host.race_service_ms == 1200);
	drain(&host);

	/* Supply a lobby transition to isolate the retained service clocks. */
	mh_net_session_reset_replication(&host);
	host.phase = MH_NET_LOBBY;

	for (i = 0; i < 2; i++) {
		host.roster.players[i].flags = MH_NET_FLAG_LOBBY;
		host.lobby.slots[i].profile.flags = MH_NET_FLAG_LOBBY;
		host.lobby.slots[i].phase = MH_NET_IN_LOBBY;
	}

	CHECK(mh_net_session_tick(&host, 1800, NULL) == MH_ERROR_NONE);
	CHECK(host.replication.status_ms == 1800);
	CHECK(host.race_service_ms == 1200);
	drain(&host);
	mh_net_session_reset_replication(&host);
	host.phase = MH_NET_LOADING;

	for (i = 0; i < 2; i++) {
		host.roster.players[i].flags = MH_NET_FLAG_RACING;
		host.lobby.slots[i].profile.flags = MH_NET_FLAG_RACING;
		host.lobby.slots[i].phase = MH_NET_IN_RACE;
	}

	configure_replication(&host);
	CHECK(mh_net_session_loaded(&host, 2000, NULL) == MH_ERROR_NONE);
	CHECK(host.race_service_ms == 1200);
	CHECK(host.replication.sent_ms[1] == 0);
	CHECK(mh_net_session_tick(&host, 2200, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 0);
	CHECK(host.replication.pending_service[1].size == 0);
	CHECK(mh_net_session_tick(&host, 2200.25, NULL) == MH_ERROR_NONE);
	CHECK(host.race_service_ms == 2200);
	CHECK(host.replication.pending_service[1].size == 103);
	CHECK(mh_net_session_tick(&host, 2267, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 1);
	CHECK(mh_net_session_outgoing(&host)->size == 103);
}

static void
patched_race_returns(void)
{
	struct mh_net_message message;
	const struct mh_net_outgoing *out;
	unsigned int i, chats, rosters;

	patched_loading(3);
	configure_replication(&host);
	CHECK(mh_net_session_loaded(&host, 0, NULL) == MH_ERROR_NONE);
	host.ready_mask = 15;
	host.replication.history[1].received_ms = 9000;
	host.replication.history[1].count = 1;
	host.replication.history[1].entries[0].stamp = 8000;

	for (i = 0; i < 3; i++) {
		configure_replication(&clients[i]);
		CHECK(mh_net_session_loaded(&clients[i], 0, NULL) ==
			MH_ERROR_NONE);
	}

	/* Non-host type 18 receipt only displays a notice on a client. */
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_RACE;
	message.player = 1;
	saved = clients[1];
	patched_message(&clients[1], 1, &message, 0);
	CHECK(clients[1].phase == saved.phase &&
		clients[1].ready_mask == saved.ready_mask);
	CHECK(!memcmp(&clients[1].roster, &saved.roster, sizeof(saved.roster)));
	CHECK(!memcmp(&clients[1].replication, &saved.replication,
		sizeof(saved.replication)));
	CHECK(clients[1].chat_count == saved.chat_count + 1);
	CHECK(!strcmp(clients[1].chat[clients[1].chat_count - 1].text,
		"Driver1 left the race."));
	CHECK(clients[1].out_count == 0);

	clients[0].now_ms = 10000;
	clients[0].out_count = MH_NET_OUTBOX;
	saved = clients[0];
	CHECK(mh_net_session_return_lobby(&clients[0], NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&clients[0], &saved, sizeof(saved)));
	clients[0].out_count = 0;
	CHECK(mh_net_session_return_lobby(&clients[0], NULL) == MH_ERROR_NONE);
	CHECK(clients[0].phase == MH_NET_LOBBY);
	CHECK(clients[0].replication.status_ms == 10000);
	out = mh_net_session_outgoing(&clients[0]);
	CHECK(out != NULL && out->reliable && out->size == 5 &&
		out->data[0] == MH_NET_LEAVE_RACE && out->data[4] == 1);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_RACE;
	message.player = 1;
	saved = host;
	CHECK(deliver(&host, 3, &message, 1, 10000, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&host, &saved, sizeof(saved)));
	CHECK(deliver(&host, 2, &message, 0, 10000, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&host, &saved, sizeof(saved)));
	host.out_count = MH_NET_OUTBOX;
	saved = host;
	CHECK(deliver(&host, 2, &message, 1, 10000, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&host, &saved, sizeof(saved)));
	host.out_count = 0;
	mh_net_session_sent(&clients[0]);
	CHECK(deliver(&host, 2, &message, 1, 10000, NULL) == MH_ERROR_NONE);
	CHECK(host.lobby.slots[1].phase == MH_NET_IN_LOBBY);
	CHECK(host.ready_mask == 15 && host.return_pending == 0);
	CHECK(host.replication.history[1].received_ms == 9000 &&
		host.replication.history[1].count == 1);
	chats = rosters = 0;

	for (i = 0; i < host.out_count; i++) {
		out = &host.outgoing[(host.out_head + i) % MH_NET_OUTBOX];
		CHECK(out->reliable);
		chats += out->data[0] == MH_NET_CHAT;
		rosters += out->data[0] == MH_NET_ROSTER;
		CHECK(out->data[0] != MH_NET_LEAVE_RACE);
	}

	CHECK(chats == 3 && rosters == 1);
	pump(10000);
	CHECK(!strcmp(
		host.chat[host.chat_count - 1].text, "Driver1 left the race."));
	CHECK(clients[1].roster.players[1].flags == MH_NET_FLAG_RACING);
	CHECK(mh_net_session_tick(&host, 10001, NULL) == MH_ERROR_NONE);
	pump(10001);
	CHECK(clients[1].roster.players[1].flags == MH_NET_FLAG_LOBBY);
	CHECK(clients[2].roster.players[1].flags == MH_NET_FLAG_LOBBY);
	CHECK(clients[1].phase == MH_NET_WAITING &&
		clients[2].phase == MH_NET_WAITING);
	host.now_ms = 11000;
	host.out_count = MH_NET_OUTBOX;
	saved = host;
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&host, &saved, sizeof(saved)));
	host.out_count = 0;
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE);
	CHECK(host.phase == MH_NET_LOBBY && host.return_pending == 12);
	CHECK(host.replication.status_ms == 11000 && host.out_count == 2);
	CHECK(host.replication.history[1].received_ms == 9000 &&
		host.replication.history[1].count == 0);

	/* A local return can cross the host's reliable end-of-race message. */
	clients[1].now_ms = 11000;
	CHECK(mh_net_session_return_lobby(&clients[1], NULL) == MH_ERROR_NONE);
	pump(11000);
	CHECK(host.return_pending == 12);

	for (i = 0; i < 3; i++)
		CHECK(clients[i].phase == MH_NET_LOBBY);

	CHECK(mh_net_session_tick(&host, 12000, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 0);
	CHECK(mh_net_session_tick(&host, 12000.25, NULL) == MH_ERROR_NONE);
	pump(12001);
	CHECK(host.return_pending == 0);

	for (i = 0; i < 3; i++)
		CHECK(!memcmp(
			&clients[i].roster, &host.roster, sizeof(host.roster)));

	CHECK(mh_net_session_begin_race(&host, 12002, NULL) == MH_ERROR_NONE);
	pump(12002);
	CHECK(host.phase == MH_NET_LOADING);

	for (i = 0; i < 3; i++)
		CHECK(clients[i].phase == MH_NET_LOADING);
}

static void
patched_return_acknowledgement(void)
{
	const struct mh_net_outgoing *out;
	struct mh_net_message message;
	unsigned long revision;
	unsigned int i;

	patched_init();

	for (i = 0; i < 3; i++)
		join(i, 0);

	/* Isolate return acknowledgements with an explicit pending barrier. */
	host.return_pending = 14;
	CHECK(mh_net_session_tick(&host, 1001, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 3);

	while ((out = mh_net_session_outgoing(&host)) != NULL) {
		CHECK(out->size == 8 && out->data[0] == MH_NET_PING);
		mh_net_session_sent(&host);
	}

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PONG;
	message.player = 1;
	message.stamp = 999;
	patched_message(&host, 2, &message, 1002);
	CHECK(host.return_pending == 14 && host.out_count == 0);
	message.type = MH_NET_LOBBY_PONG;
	message.player = 2;
	saved = host;
	CHECK(deliver(&host, 2, &message, 0, 1002, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&host, &saved, sizeof(host)));
	message.player = 1;
	host.out_count = MH_NET_OUTBOX;
	saved = host;
	CHECK(deliver(&host, 2, &message, 0, 1002, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&host, &saved, sizeof(host)));
	host.out_count = 0;
	patched_message(&host, 2, &message, 1002);
	CHECK(host.return_pending == 12 && host.out_count == 3);
	CHECK(host.connections[0].ping_ms == 3 &&
		host.connections[0].contact_ms == 1002);

	while ((out = mh_net_session_outgoing(&host)) != NULL) {
		CHECK(out->reliable && out->size == 1103 &&
			out->data[0] == MH_NET_ROSTER);
		mh_net_session_sent(&host);
	}

	revision = host.roster_revision;
	patched_message(&host, 2, &message, 1003);
	CHECK(host.return_pending == 12 && host.out_count == 0 &&
		host.roster_revision == revision);

	for (i = 2; i <= 3; i++) {
		message.player = i;
		patched_message(&host, i + 1, &message, 1002 + i);
		drain(&host);
	}

	CHECK(host.return_pending == 0);
	CHECK(mh_net_session_tick(&host, 2002, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 6);
}

static void
patched_roster_lifetime(void)
{
	struct mh_net_message message;

	patched_init();
	join(0, 0);
	clients[0].replication.history[7].count = 1;
	clients[0].replication.history[7].entries[0].stamp = 40;
	clients[0].replication.history[7].received_ms = 50;
	clients[0].replication.status[7].ping_ms = 700;
	clients[0].replication.status[7].flags = MH_NET_FLAG_UNAVAILABLE;
	clients[0].ready_mask = 255;
	clients[0].return_pending = 128;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_ROSTER;
	message.roster = host.roster;
	patched_message(&clients[0], 1, &message, 100);
	CHECK(clients[0].roster.players[7].flags == 0);
	CHECK(clients[0].ready_mask == 0 && clients[0].return_pending == 128);
	CHECK(clients[0].replication.history[7].count == 1 &&
		clients[0].replication.history[7].received_ms == 50 &&
		clients[0].replication.history[7].entries[0].stamp == 40);
	CHECK(clients[0].replication.status[7].ping_ms == 700 &&
		clients[0].replication.status[7].flags ==
			MH_NET_FLAG_UNAVAILABLE);
	message.roster.players[7] = session_config(7).profile;
	message.roster.players[7].flags = MH_NET_FLAG_LOBBY;

	/* Original roster ready bit. */
	message.roster.players[0].flags |= 0x10;
	patched_message(&clients[0], 1, &message, 101);
	CHECK(clients[0].ready_mask == 1 &&
		clients[0].roster.players[7].flags == MH_NET_FLAG_LOBBY);
	CHECK(clients[0].replication.history[7].received_ms == 50 &&
		clients[0].replication.history[7].count == 1);
	CHECK(clients[0].replication.status[7].ping_ms == 700);
}

static void
patched_slot_reuse(void)
{
	struct mh_net_session_config settings;
	struct mh_net_message message;
	unsigned int i;

	patched_loading(2);
	configure_replication(&host);
	CHECK(mh_net_session_loaded(&host, 0, NULL) == MH_ERROR_NONE);
	host.ready_mask = 7;
	CHECK(mh_net_session_status(&host, 1, 3, 9, NULL) == MH_ERROR_NONE);
	host.return_pending = 2;
	host.replication.history[1].count = 1;
	host.replication.history[1].entries[0].stamp = 40;
	host.replication.history[1].received_ms = 50;

	for (i = 0; i < 2; i++) {
		configure_replication(&clients[i]);
		CHECK(mh_net_session_loaded(&clients[i], 0, NULL) ==
			MH_ERROR_NONE);
	}

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PONG;
	message.player = 1;
	message.stamp = 23;
	patched_message(&host, 2, &message, 100);
	CHECK(host.replication.status[1].ping_ms == 77);
	CHECK(mh_net_session_leave(&clients[0], NULL) == MH_ERROR_NONE);
	pump(101);
	CHECK(host.lobby.slots[1].phase == MH_NET_EMPTY &&
		host.return_pending == 2);
	CHECK(host.replication.history[1].count == 1);
	CHECK(mh_net_session_tick(&host, 1001, NULL) == MH_ERROR_NONE);
	pump(1001);
	CHECK(host.replication.status[1].ping_ms == 77 &&
		clients[1].replication.status[1].ping_ms == 77);
	CHECK(clients[1].replication.status[1].value_a == 3 &&
		clients[1].replication.status[1].value_b == 9);
	settings = session_config(1);
	settings.wire = MH_NET_WIRE_31_LOBBY;
	settings.provider = MH_NET_PROVIDER_LANINET;
	strcpy(settings.profile.name, "Replacement");
	settings.profile.values[MH_NET_PROFILE_RACES] = 20;
	settings.profile.values[MH_NET_PROFILE_POINTS] = 100;
	CHECK(mh_net_session_init(0, &settings, &clients[0], NULL) ==
		MH_ERROR_NONE);
	join(0, 1100);
	CHECK(host.phase == MH_NET_WAITING && clients[0].phase == MH_NET_LOBBY);
	CHECK(clients[0].player == 1 &&
		host.lobby.slots[1].phase == MH_NET_IN_LOBBY);
	CHECK(!strcmp(host.roster.players[1].name, "Replacement"));
	CHECK(host.roster.players[1].value_a == 3 &&
		host.roster.players[1].value_b == 9);
	CHECK(clients[0].roster.players[1].value_a == 3 &&
		clients[0].roster.players[1].value_b == 9);
	CHECK(host.roster.players[1].values[MH_NET_PROFILE_RACES] == 0 &&
		host.roster.players[1].values[MH_NET_PROFILE_POINTS] == 0);
	CHECK(host.connections[0].contact_ms == 1100 &&
		host.connections[0].ping_ms == 77);
	CHECK(host.replication.history[1].count == 1 &&
		host.replication.history[1].entries[0].stamp == 40 &&
		host.replication.history[1].received_ms == 50);
	CHECK(host.return_pending == 2);
	CHECK(mh_net_session_return_lobby(&host, NULL) == MH_ERROR_NONE);
	pump(1101);
	CHECK(host.return_pending == 4);
	CHECK(mh_net_session_begin_race(&host, 1102, NULL) == MH_ERROR_STATE);
	CHECK(mh_net_session_tick(&host, 2101, NULL) == MH_ERROR_NONE);
	pump(2101);
	CHECK(host.return_pending == 0);
	host.return_pending = 128; /* Absent slot retains an old pending bit. */
	saved = host;
	CHECK(mh_net_session_begin_race(&host, 2102, NULL) == MH_ERROR_NONE);
	CHECK(host.phase == MH_NET_LOADING);
	host = saved;
	host.return_pending |= 2;
	CHECK(mh_net_session_begin_race(&host, 2102, NULL) == MH_ERROR_STATE);
}

static void
patched_timeout_cleanup(void)
{
	struct mh_net_connection connection;
	const struct mh_net_outgoing *out;
	unsigned long revision;
	unsigned int i, action;
	static const unsigned long peers[] = {3, 5, 4, 5, 2};
	static const int disconnects[] = {1, 0, 1, 0, 1};

	patched_loading(4);
	configure_replication(&host);
	CHECK(mh_net_session_loaded(&host, 0, NULL) == MH_ERROR_NONE);
	host.ready_mask = 21;
	host.return_pending = 30;
	host.lobby.slots[1].phase = MH_NET_IN_LOBBY;
	host.lobby.slots[1].profile.flags = MH_NET_FLAG_LOBBY;
	host.roster.players[1].flags = MH_NET_FLAG_LOBBY;
	host.connections[3].contact_ms = 130000;

	/*
	 * Connection allocation order must not change the original slot scan.
	 */
	connection = host.connections[1];
	host.connections[1] = host.connections[2];
	host.connections[2] = connection;
	host.replication.view[4] = 2;

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		host.replication.history[i].received_ms = 9000;
		host.replication.history[i].count = 1;
		host.replication.history[i].entries[0].stamp = 8000;
	}

	revision = host.chat_revision;
	host.out_count = MH_NET_OUTBOX;

	for (i = 0; i < MH_NET_OUTBOX; i++)
		host.outgoing[i].peer = 99;

	saved = host;
	CHECK(mh_net_session_tick(&host, 130000, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&host, &saved, sizeof(saved)));
	host.out_count = 0;
	CHECK(mh_net_session_tick(&host, 130000, NULL) == MH_ERROR_NONE);
	CHECK(host.chat_revision == revision + 2);
	CHECK(!strcmp(
		host.chat[host.chat_count - 2].text, "Driver2 timed out!"));
	CHECK(!strcmp(
		host.chat[host.chat_count - 1].text, "Driver3 timed out!"));
	CHECK(host.ready_mask == 17 && host.return_pending == 30);
	CHECK(host.replication.view[4] == 2);

	for (i = 1; i <= 3; i++) {
		CHECK(host.lobby.slots[i].phase == MH_NET_EMPTY &&
			host.roster.players[i].flags == 0);
		CHECK(host.replication.history[i].received_ms == 9000 &&
			host.replication.history[i].count == 1 &&
			host.replication.history[i].entries[0].stamp == 8000);
		CHECK(host.connections[i - 1].closing &&
			host.connections[i - 1].contact_ms == 0);
	}

	action = 0;

	while ((out = mh_net_session_outgoing(&host)) != NULL) {
		if (out->reliable) {
			CHECK(action < sizeof(peers) / sizeof(peers[0]));

			if (action >= sizeof(peers) / sizeof(peers[0]))
				return;

			CHECK(out->peer == peers[action] &&
				out->disconnect == disconnects[action]);

			if (out->disconnect)
				CHECK(out->size == 0);
			else {
				CHECK(out->data[0] == MH_NET_CHAT);
				CHECK(!strcmp((const char *)out->data + 9,
					action == 1 ? "Driver2 timed out!"
						: "Driver3 timed out!"));
			}

			action++;
		}

		mh_net_session_sent(&host);
	}

	CHECK(action == 5);

	/* A provider loss has no fabricated type-17 relay or lobby roster. */
	revision = host.chat_revision;
	CHECK(mh_net_session_disconnected(&host, 5, 130001, NULL) ==
		MH_ERROR_NONE);
	CHECK(host.out_count == 0 && host.chat_revision == revision);
	CHECK(host.roster.players[4].flags == 0 &&
		!strcmp(host.roster.players[4].name, "Driver4"));
	CHECK(host.replication.history[4].received_ms == 9000 &&
		host.replication.history[4].count == 1 &&
		host.return_pending == 30);

	patched_loading(1);
	configure_replication(&clients[0]);
	CHECK(mh_net_session_loaded(&clients[0], 0, NULL) == MH_ERROR_NONE);
	clients[0].ready_mask = 255;
	clients[0].return_pending = 165;
	clients[0].contact_ms = 123;
	clients[0].replication.pending_service[0].size = 8;
	CHECK(mh_net_session_chat(&clients[0], "pending", 0xffffffUL, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		clients[0].replication.history[i].received_ms = 9000;
		clients[0].replication.history[i].count = 1;
		clients[0].replication.history[i].entries[0].stamp = 8000;
	}

	CHECK(mh_net_session_tick(&clients[0], 130000, NULL) == MH_ERROR_NONE);
	CHECK(clients[0].phase == MH_NET_CLOSED && clients[0].ready_mask == 0);
	CHECK(clients[0].return_pending == 165 && clients[0].contact_ms == 123);
	CHECK(clients[0].replication.pending_service[0].size == 0);
	CHECK(!strcmp(clients[0].chat[clients[0].chat_count - 1].text,
		"Server connection timed out!"));

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		CHECK(clients[0].roster.players[i].flags == 0);
		CHECK(clients[0].replication.history[i].count == 1 &&
			clients[0].replication.history[i].received_ms == 9000 &&
			clients[0].replication.history[i].entries[0].stamp ==
				8000);
	}

	CHECK(clients[0].out_count == 1);
	out = mh_net_session_outgoing(&clients[0]);
	CHECK(out != NULL && out->peer == 1 && out->disconnect &&
		out->size == 0);
}

static void
patched_race_departures(void)
{
	struct mh_net_message message;
	struct mh_net_outgoing departure;
	const struct mh_net_outgoing *out;
	unsigned int i;

	patched_loading(3);
	configure_replication(&host);
	CHECK(mh_net_session_loaded(&host, 0, NULL) == MH_ERROR_NONE);
	host.ready_mask = 15;
	host.return_pending = 2;
	host.replication.history[1].received_ms = 9000;
	host.replication.history[1].count = 1;
	host.replication.history[1].entries[0].stamp = 8000;
	host.replication.view[2] = 1;
	host.lobby.slots[3].phase = MH_NET_IN_LOBBY;
	host.lobby.slots[3].profile.flags = MH_NET_FLAG_LOBBY;
	host.roster.players[3].flags = MH_NET_FLAG_LOBBY;

	for (i = 0; i < 3; i++) {
		clients[i].roster = host.roster;

		if (i < 2) {
			configure_replication(&clients[i]);
			CHECK(mh_net_session_loaded(&clients[i], 0, NULL) ==
				MH_ERROR_NONE);
		}
	}

	clients[2].phase = MH_NET_LOBBY;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_LOBBY;
	message.player = 1;

	/* Failure cannot publish removal before its notice/close is queued. */
	host.out_count = MH_NET_OUTBOX;

	for (i = 0; i < MH_NET_OUTBOX; i++)
		host.outgoing[i].peer = 99;

	saved = host;
	CHECK(deliver(&host, 2, &message, 1, 10000, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&host, &saved, sizeof(saved)));
	host.out_count = 0;
	CHECK(mh_net_session_leave(&clients[0], NULL) == MH_ERROR_NONE);
	out = mh_net_session_outgoing(&clients[0]);
	CHECK(out != NULL && out->size == 5 && out->reliable &&
		out->disconnect && out->data[0] == MH_NET_LEAVE_LOBBY &&
		out->data[4] == 1);
	drain(&clients[0]);
	CHECK(deliver(&host, 2, &message, 1, 10000, NULL) == MH_ERROR_NONE);
	CHECK(host.out_count == 2 && host.chat_count == saved.chat_count);
	CHECK(host.ready_mask == 13 && host.return_pending == 2);
	CHECK(host.roster.players[1].flags == 0 &&
		!strcmp(host.roster.players[1].name, "Driver1"));
	CHECK(host.lobby.slots[1].phase == MH_NET_EMPTY &&
		host.lobby.slots[1].peer == 0);
	CHECK(host.replication.history[1].count == 1 &&
		host.replication.history[1].received_ms == 9000 &&
		host.replication.history[1].entries[0].stamp == 8000 &&
		host.replication.view[2] == 1);
	out = mh_net_session_outgoing(&host);
	CHECK(out != NULL && out->peer == 2 && out->disconnect &&
		out->size == 0);
	mh_net_session_sent(&host);
	out = mh_net_session_outgoing(&host);
	CHECK(out != NULL && out->peer == 4 && !out->disconnect &&
		out->data[0] == MH_NET_CHAT);
	CHECK(mh_net_session_disconnected(&host, 2, 10000, NULL) ==
		MH_ERROR_NONE);
	pump(10000);
	CHECK(!strcmp(clients[2].chat[clients[2].chat_count - 1].text,
		"Driver1 left the lobby."));
	CHECK(clients[1].roster.players[1].flags == MH_NET_FLAG_RACING);
	CHECK(mh_net_session_tick(&host, 10001, NULL) == MH_ERROR_NONE);
	pump(10001);
	CHECK(clients[1].roster.players[1].flags == 0);
	CHECK(host.replication.history[1].received_ms == 9000);

	/* Receipt removes only the named slot, even when it is the host. */
	clients[1].replication.history[1] = host.replication.history[1];
	clients[1].ready_mask = 15;
	clients[1].return_pending = 2;
	saved = clients[1];
	CHECK(deliver(&clients[1], 1, &message, 1, 10001, NULL) ==
		MH_ERROR_NONE);
	CHECK(clients[1].phase == MH_NET_WAITING &&
		clients[1].ready_mask == 13 && clients[1].return_pending == 2);
	CHECK(!memcmp(&clients[1].replication.history[1],
		&saved.replication.history[1],
		sizeof(saved.replication.history[1])));
	CHECK(clients[1].chat_revision == saved.chat_revision);
	out = mh_net_session_outgoing(&clients[1]);
	CHECK(out != NULL && out->peer == 1 && out->reliable &&
		out->data[0] == MH_NET_CHAT && out->data[4] == 2 &&
		!strcmp((const char *)out->data + 9,
			"Driver1 left the lobby."));
	drain(&clients[1]);
	drain(&host);
	CHECK(mh_net_session_leave(&host, NULL) == MH_ERROR_NONE);
	out = mh_net_session_outgoing(&host);
	CHECK(out != NULL && out->peer == 3 && out->reliable &&
		out->disconnect && out->size == 5 &&
		out->data[0] == MH_NET_LEAVE_LOBBY && out->data[4] == 0);

	if (out == NULL)
		return;

	departure = *out;
	mh_net_session_sent(&host);
	CHECK(mh_net_session_receive(&clients[1], 1, departure.data,
		departure.size, 1, 10001, NULL) == MH_ERROR_NONE);
	CHECK(clients[1].phase == MH_NET_WAITING &&
		clients[1].roster.players[0].flags == 0 &&
		clients[1].roster.players[2].flags == MH_NET_FLAG_RACING);
	CHECK(mh_net_session_disconnected(&clients[1], 1, 10001, NULL) ==
		MH_ERROR_NONE);
	CHECK(clients[1].phase == MH_NET_CLOSED);
	out = mh_net_session_outgoing(&host);
	CHECK(out != NULL && out->peer == 4 && out->disconnect &&
		out->size == 0);
}

static void
patched_departure(void)
{
	const unsigned char member_leave[] = {17, 0, 5, 0, 2};
	const struct mh_net_outgoing *out;
	struct mh_net_outgoing kick;
	unsigned long chat_revision;
	unsigned int i, j;

	patched_init();
	join(0, 0);
	join(1, 0);
	saved = clients[0];
	CHECK(mh_net_session_receive(&clients[0], 1, member_leave,
		sizeof(member_leave), 1, clients[0].now_ms,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&clients[0], &saved, sizeof(saved)) == 0);
	host.out_count = MH_NET_OUTBOX;
	saved = host;
	CHECK(mh_net_session_leave(&host, NULL) == MH_ERROR_RANGE);
	CHECK(memcmp(&host, &saved, sizeof(host)) == 0);
	drain(&host);
	CHECK(mh_net_session_chat(&host, "Goodbye", 0xffffffUL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_session_leave(&host, NULL) == MH_ERROR_NONE);
	CHECK(host.phase == MH_NET_CLOSED && host.out_count == 4);

	for (i = 0; i < 2; i++) {
		out = mh_net_session_outgoing(&host);
		CHECK(out != NULL && out->data[0] == 7 && !out->disconnect);
		mh_net_session_sent(&host);
	}

	for (i = 0; i < 2; i++) {
		out = mh_net_session_outgoing(&host);
		CHECK(out != NULL && out->peer == i + 2 && out->size == 5 &&
			out->data[0] == 17 && out->data[4] == 0 &&
			out->reliable && out->disconnect);

		if (out == NULL)
			return;

		CHECK(mh_net_session_receive(&clients[i], 1, out->data,
			out->size, 1, 1, NULL) == MH_ERROR_NONE);
		CHECK(clients[i].phase == MH_NET_CLOSED);

		for (j = 0; j < MH_NET_PLAYERS; j++)
			CHECK(clients[i].roster.players[j].flags == 0);

		CHECK(strcmp(clients[i].roster.players[0].name, "Driver0") ==
			0);
		mh_net_session_sent(&host);
	}

	patched_init();
	join(0, 0);
	join(1, 0);
	chat_revision = host.chat_revision;
	CHECK(mh_net_session_kick(&host, 1, NULL) == MH_ERROR_NONE);
	CHECK(host.chat_revision == chat_revision &&
		host.roster.players[1].flags == 0);
	out = mh_net_session_outgoing(&host);
	CHECK(out != NULL && out->peer == 2 && out->size == 4 &&
		out->data[0] == 19 && out->reliable && out->disconnect);

	if (out == NULL)
		return;

	kick = *out;
	mh_net_session_sent(&host);
	clients[0].out_count = MH_NET_OUTBOX;
	saved = clients[0];
	CHECK(mh_net_session_receive(&clients[0], 1, kick.data, kick.size, 1, 1,
		NULL) == MH_ERROR_RANGE);
	CHECK(memcmp(&clients[0], &saved, sizeof(saved)) == 0);
	drain(&clients[0]);
	CHECK(mh_net_session_receive(&clients[0], 1, kick.data, kick.size, 1, 1,
		NULL) == MH_ERROR_NONE);
	CHECK(clients[0].phase == MH_NET_CLOSED && clients[0].kicked);
	out = mh_net_session_outgoing(&clients[0]);
	CHECK(out != NULL && out->peer == 1 && out->size == 5 &&
		out->data[0] == 17 && out->data[4] == 1 && out->reliable &&
		out->disconnect);

	for (i = 0; i < MH_NET_PLAYERS; i++)
		CHECK(clients[0].roster.players[i].flags == 0);

	/*
	 * The removed host endpoint no longer receives the final client reply.
	 */
	drain(&clients[0]);
	pump(1);
	CHECK(clients[1].phase == MH_NET_LOBBY &&
		clients[1].roster.players[1].flags == 0);
	CHECK(mh_net_session_leave(&clients[1], NULL) == MH_ERROR_NONE);
	out = mh_net_session_outgoing(&clients[1]);
	CHECK(out != NULL && out->size == 5 && out->data[0] == 17 &&
		out->data[4] == 2 && out->reliable && out->disconnect);
}

static void
patched_console_identity(void)
{
	struct mh_net_message message;
	unsigned char data[MH_NET_WIRE_PACKET_LIMIT];
	unsigned int dedicated;
	size_t size;

	for (dedicated = 0; dedicated < 2; dedicated++) {
		patched_init();
		join(0, 0);
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_ROSTER;
		message.roster = host.roster;

		if (dedicated)
			message.roster.players[0].flags &= ~MH_NET_FLAG_PRESENT;

		CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
			sizeof(data), &size, NULL) == MH_ERROR_NONE);
		CHECK(mh_net_session_receive(&clients[0], 1, data, size, 1, 1,
			NULL) == MH_ERROR_NONE);
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_PROFILE_UPDATE;
		message.profile = host.roster.players[0];
		strcpy(message.profile.name, "Renamed");
		CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
			sizeof(data), &size, NULL) == MH_ERROR_NONE);
		CHECK(mh_net_session_receive(&clients[0], 1, data, size, 1, 2,
			NULL) == MH_ERROR_NONE);
		CHECK(!strcmp(clients[0].roster.players[0].name, "Renamed"));
		CHECK((clients[0].roster.players[0].flags &
			MH_NET_FLAG_PRESENT) ==
			(dedicated ? 0 : MH_NET_FLAG_LOBBY));
		CHECK((clients[0].replication.status[0].flags &
			MH_NET_FLAG_PRESENT) ==
			(dedicated ? 0 : MH_NET_FLAG_LOBBY));
		CHECK(!dedicated || !(clients[0].ready_mask & 1));
	}
}

static void
dedicated_roster_view(void)
{
	struct mh_net_message message;
	struct mh_lobby_roster roster;
	struct mh_lobby_roster_view view;
	unsigned int i, advance[256], names, ready, passing;
	unsigned char data[MH_NET_WIRE_PACKET_LIMIT], palette[768];
	size_t size;

	patched_init();
	join(0, 0);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_ROSTER;
	message.roster = host.roster;
	message.roster.player_count = MH_NET_WIRE_PLAYERS;
	message.roster.players[0].flags = 0;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		message.roster.players[i] = host.config.profile;
		message.roster.players[i].flags = MH_NET_FLAG_LOBBY |
			(i % 3 == 0		     ? MH_NET_FLAG_READY
					: i % 3 == 1 ? MH_NET_FLAG_PASS
						: 0);
	}

	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_receive(&clients[0], 1, data, size, 1, 2, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_lobby_session_roster(&clients[0], "Dedicated", 0, &roster,
		NULL) == MH_ERROR_NONE);
	CHECK(roster.layout == MH_LOBBY_ROSTER_31 && roster.count == 13);
	CHECK(!roster.players[0].lobby && !roster.players[0].racing);

	for (i = 1; i < roster.count; i++) {
		CHECK(roster.players[i].ready == (i % 3 == 0));
		CHECK(roster.players[i].pass == (i % 3 == 1));
	}

	memset(palette, 0, sizeof(palette));

	for (i = 0; i < 256; i++)
		advance[i] = 6;

	CHECK(mh_lobby_roster_view(&roster, advance, palette, &view, NULL) ==
		MH_ERROR_NONE);
	names = ready = passing = 0;

	for (i = 0; i < view.count; i++) {
		if (view.lines[i].x == 75) {
			CHECK(view.lines[i].y == 165 + names * 10);
			names++;
		}

		if (view.lines[i].x == 50) {
			CHECK(view.lines[i].y >= 167 && view.lines[i].y <= 277);
			ready += strcmp((char *)view.lines[i].text, "R") == 0;
			passing += strcmp((char *)view.lines[i].text, "P") == 0;
		}
	}

	CHECK(names == 12 && ready == 4 && passing == 4);
}

static void
patched_sessions(void)
{
	struct mh_net_message message;
	unsigned char data[MH_NET_WIRE_PACKET_LIMIT];
	size_t size;

	patched_init();

	join(0, 0);
	CHECK(clients[0].phase == MH_NET_LOBBY);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_ROSTER;
	message.roster = host.roster;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(size == 1103);

	/*
	 * More than 1024 bytes, including multiple complete application frames.
	 */
	memcpy(data + size, data, size);
	CHECK(mh_net_session_receive(&clients[0], 1, data, size * 2, 1, 1,
		NULL) == MH_ERROR_NONE);
	saved = clients[0];
	CHECK(mh_net_session_receive(&clients[0], 1, data, size * 2 - 1, 1, 2,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);

	/* Patched lobby clients accept the complete dedicated wire roster. */
	message.roster.player_count = MH_NET_WIRE_PLAYERS;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_receive(&clients[0], 1, data, size, 1, 2, NULL) ==
		MH_ERROR_NONE);
	CHECK(clients[0].roster.player_count == MH_NET_WIRE_PLAYERS);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_LOBBY;
	message.player = 12;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_receive(&clients[0], 1, data, size, 1, 2, NULL) ==
		MH_ERROR_NONE);
	saved = clients[0];

	/*
	 * Original-sized text is accepted; larger wire fields cannot overrun UI
	 * text.
	 */
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_NOTICE;
	memset(message.text, 'x', MH_NET_CHAT_BYTES);
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_receive(&clients[0], 1, data, size, 1, 2, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);

	/*
	 * A setup without unique grid ranks is rejected atomically.
	 */
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_RACE_CONFIG;
	message.roster = host.roster;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_receive(&clients[0], 1, data, size, 1, 2, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);
	data[0] = MH_NET_RACE_CONFIG;
	data[1] = 0;
	data[2] = 4;
	data[3] = 0;
	CHECK(mh_net_session_receive(&clients[0], 1, data, 4, 1, 2, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&saved, &clients[0], sizeof(saved)) == 0);

	/*
	 * A failed multi-recipient update restores both profile and pending
	 * bytes.
	 */
	host.out_count = MH_NET_OUTBOX;
	saved = host;
	CHECK(mh_net_session_profile(&host, &host.config.profile, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(&saved, &host, sizeof(saved)) == 0);
}

static void
fixed_host_timing(unsigned int rate)
{
	struct mh_net_session_config settings;

	/*
	 * The shared pump scans every slot; unused clients must have no mail.
	 */
	memset(clients, 0, sizeof(clients));
	settings = session_config(0);
	settings.wire = MH_NET_WIRE_31_LOBBY;
	settings.simulation_rate = rate;
	CHECK(mh_net_session_init(1, &settings, &host, NULL) == MH_ERROR_NONE);

	settings = session_config(1);
	settings.wire = MH_NET_WIRE_31_LOBBY;
	settings.simulation_rate = 120;
	CHECK(mh_net_session_init(0, &settings, &clients[0], NULL) ==
		MH_ERROR_NONE);
	join(0, 10);
	CHECK(clients[0].phase == MH_NET_LOBBY);
	CHECK(clients[0].config.simulation_rate == rate);
	CHECK(host.config.simulation_rate == rate);

	if (mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) == NULL)
		return;

	/* Original peers cannot silently join a fixed-tick session. */
	settings = session_config(2);
	settings.wire = MH_NET_WIRE_31_LOBBY;
	settings.simulation_rate = 0;
	CHECK(mh_net_session_init(0, &settings, &clients[1], NULL) ==
		MH_ERROR_NONE);
	join(1, 20);
	CHECK(clients[1].phase == MH_NET_REJECTED);
	CHECK(clients[1].rejection_reason == 3);
	CHECK(strstr(clients[1].rejection, "4.0") != NULL);
}

static void
legacy_host_rejects_fixed_client(void)
{
	struct mh_net_session_config settings;

	memset(clients, 0, sizeof(clients));
	settings = session_config(0);
	settings.wire = MH_NET_WIRE_31_LOBBY;
	settings.simulation_rate = 0;
	CHECK(mh_net_session_init(1, &settings, &host, NULL) == MH_ERROR_NONE);

	settings = session_config(1);
	settings.wire = MH_NET_WIRE_31_LOBBY;
	settings.simulation_rate = 120;
	CHECK(mh_net_session_init(0, &settings, &clients[0], NULL) ==
		MH_ERROR_NONE);
	join(0, 10);
	CHECK(clients[0].phase == MH_NET_REJECTED);
	CHECK(clients[0].rejection_reason == 3);
}

static void
fixed_timing_sessions(void)
{
	const unsigned int rates[] = {60, 120, 240, 100, 144, 1, 0xffffffffU};
	unsigned int i;
	int previous_failures;

	for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
		previous_failures = failures;
		fixed_host_timing(rates[i]);

		if (failures != previous_failures)
			fprintf(stderr, "fixed host: simulation=%u Hz\n",
				rates[i]);
	}

	if (mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) == NULL)
		return;

	previous_failures = failures;
	legacy_host_rejects_fixed_client();

	if (failures != previous_failures)
		fprintf(stderr, "legacy host: rejecting a 120 Hz client\n");
}

static void
join_password_proof(void)
{
	struct mh_net_session_config config;
	struct mh_net_challenge challenge;
	struct mh_net_message message, reply;
	struct mh_net_frame frame;
	struct mh_net_reader reader;
	const struct mh_net_outgoing *out;

	config = session_config(1);
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.simulation_rate = MH_SIM_RATE_DEFAULT;
	memset(&challenge, 0, sizeof(challenge));
	challenge.active = 1;
	challenge.nonce[0] = 7;
	CHECK(mh_net_session_init(0, &config, &clients[1], NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_session_connected_from(
		&clients[1], 1, 0, &challenge, NULL) == MH_ERROR_NONE);
	out = mh_net_session_outgoing(&clients[1]);
	CHECK(out != NULL);

	if (out == NULL)
		return;

	CHECK(mh_net_wire_begin(config.wire, out->data, out->size, &reader,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_net_wire_next(config.wire, &reader, &frame) == 1);
	CHECK(mh_net_wire_decode(config.wire, &frame, &message, NULL) ==
		MH_ERROR_NONE);
	CHECK(message.type == MH_NET_JOIN && message.join.has_proof &&
		message.join.password[0] == 0);
	message.join.challenge = challenge;
	CHECK(mh_net_join_check_timing(config.provider, config.password,
		config.simulation_rate, &message.join, &reply,
		NULL) == MH_ERROR_NONE &&
		reply.type == MH_NET_ACCEPT);
	message.join.challenge.nonce[0]++;
	CHECK(mh_net_join_check_timing(config.provider, config.password,
		config.simulation_rate, &message.join, &reply,
		NULL) == MH_ERROR_NONE &&
		reply.type == MH_NET_REJECT);

	message.join.has_proof = 0;
	strcpy(message.join.password, config.password);
	CHECK(mh_net_join_check_timing(config.provider, config.password,
		config.simulation_rate, &message.join, &reply,
		NULL) == MH_ERROR_NONE &&
		reply.type == MH_NET_REJECT);
}

int
main(void)
{

	if (mh_net_protocol_get(MH_NET_PROTOCOL_OPENMH) != NULL) {
		join_password_proof();
		fixed_timing_sessions();
	}

	if (mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) == NULL)
		return failures != 0;

	lobby_notifications();
	full_lobby();
	invalid_and_backpressure();
	phases_and_timeouts();
	connection_rollback();
	disconnect_output_order();
	race_start();
	race_invalid_and_disconnect();
	race_start_clocks();
	return_before_start_receipt();
	mixed_participation();
	kicking();
	replication();
	replication_rates();
	stream_boundaries();
	round_completion(0);
	round_completion(1);
	announcements();
	announcement_batches();
	lobby_commands();
	lobby_keys();
	return_acknowledgement();
	lobby_status_feed();
	admission_cars();
	patched_profiles();
	patched_liveness();
	patched_readiness();
	patched_receipt();
	patched_race_batches();
	patched_race_service();
	patched_client_backpressure();
	race_receipt_lifetime();
	patched_race_returns();
	patched_service_lifetime();
	patched_return_acknowledgement();
	patched_roster_lifetime();
	patched_setup();
	patched_score_delivery();
	patched_slot_reuse();
	patched_timeout_cleanup();
	patched_race_departures();
	patched_departure();
	dedicated_roster_view();
	patched_sessions();
	patched_console_identity();

	return failures != 0;
}
