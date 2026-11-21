#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include "backends/network/browser_provider.h"
#else
#include "backends/clock/system_provider.h"
#include "backends/network/posix_provider.h"
#include "backends/network/udp_transport_provider.h"
#include "backends/network/windows_provider.h"
#include "engine/core/arguments.h"
#include "interfaces/clock/clock.h"
#include "interfaces/network/address.h"
#endif
#include "engine/network/network_session.h"
#include "interfaces/network/network.h"

enum {
	MIXED_POLL_EVENTS = 64,
	MIXED_RACE_MS = 2000,
	MIXED_TIMEOUT_MS = 45000
};

static struct mh_network *network;
static int ready_sent, returned, saw_race, saw_snapshot;
static double race_origin, racing_since;
static const char *driver_name, *other_name;

int	mh_mixed_client_open(const char *endpoint, unsigned int port);
int	mh_mixed_client_step(void);
void	mh_mixed_client_close(void);

int
mh_mixed_client_open(const char *endpoint, unsigned int port)
{
	struct mh_transport_target *target;
	struct mh_net_session_config config;
	const struct mh_transport_provider *provider;
	struct mh_error error;
	enum mh_error_code status;
#ifdef __EMSCRIPTEN__
	struct mh_browser_network_config remote;

	(void)port;
	memset(&remote, 0, sizeof(remote));
	remote.signaling_url = endpoint;
	provider = mh_browser_network_provider();
	status = mh_browser_network_target(&remote, &target, &error);
	driver_name = "Browser";
	other_name = "Native";
#else
	static struct mh_udp_transport_config transport_config;
	static struct mh_transport_provider native_provider;

	memset(&transport_config, 0, sizeof(transport_config));
	transport_config.clock = mh_system_clock_source();
#ifdef _WIN32
	transport_config.resolver = mh_windows_resolver_provider();
#else
	transport_config.resolver = mh_posix_resolver_provider();
#endif
	transport_config.port_policy = MH_UDP_PORTS_SHARED;
	provider = &native_provider;
	status = mh_udp_transport_init(
		&native_provider, &transport_config, &error);

	if (status == MH_ERROR_NONE)
		status = mh_transport_target_address(
			provider, endpoint, port, &target, &error);

	driver_name = "Native";
	other_name = "Browser";

#endif
	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 0;
	}

	memset(&config, 0, sizeof(config));
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.provider = MH_NET_PROVIDER_LANINET;
	config.simulation_rate = 120;
	config.car_count = 10;
	config.laps = 1;
	config.profile.update_rate = 30;
	strcpy(config.track, "Fixture");
	strcpy(config.profile.car, "Kobra X3");
	strcpy(config.profile.name, driver_name);
	status = mh_network_open_from(
		provider, 0, NULL, target, &config, &network, &error);
	mh_transport_target_close(target);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);
	else
		mh_network_set_trace(network, 1);

	return status == MH_ERROR_NONE;
}

int
mh_mixed_client_step(void)
{
	struct mh_net_session *session;
	struct mh_network_issue issue;
	struct mh_net_vehicle vehicle;
	struct mh_error error;
	double positions[MH_NET_WIRE_PLAYERS][3];
	double now;
	unsigned int i, other, present;
	char command[64];

	if (mh_network_drain(network, MIXED_POLL_EVENTS, &issue, &error) !=
		MH_ERROR_NONE)
		goto failed;

	if (issue.reason.code != MH_ERROR_NONE) {
		error = issue.reason;
		goto failed;
	}

	session = mh_network_session(network);
	now = mh_network_now(network);

	if (session->phase == MH_NET_CLOSED ||
		session->phase == MH_NET_REJECTED) {
		fprintf(stderr, "%s connection ended: %s\n", driver_name,
			session->rejection);
		return -1;
	}

	other = present = 0;

	for (i = 1; i < session->roster.player_count; i++) {
		if (session->roster.players[i].flags & MH_NET_FLAG_LOBBY)
			present++;

		if (strcmp(session->roster.players[i].name, other_name) == 0)
			other = i;
	}

	if (session->phase == MH_NET_LOBBY && !ready_sent && present == 2 &&
		other != 0) {
		sprintf(command, "%s: /ready", driver_name);

		if (mh_net_session_chat(session, command, 0, &error) !=
			MH_ERROR_NONE)
			goto failed;

		ready_sent = 1;
	}

	if (session->phase == MH_NET_LOADING) {
		memset(positions, 0, sizeof(positions));
		race_origin = now;

		if (mh_net_session_configure_replication(session, 180,
			(const double (*)[3])positions,
			&error) != MH_ERROR_NONE ||
			mh_net_session_loaded(session, now, &error) !=
				MH_ERROR_NONE)
			goto failed;
	}

	if (session->phase == MH_NET_WAITING ||
		session->phase == MH_NET_RACING) {
		memset(&vehicle, 0, sizeof(vehicle));
		vehicle.position[0] =
			strcmp(driver_name, "Browser") == 0 ? 20 : 10;

		if (mh_net_session_publish(session, &vehicle, session->player,
			&error) != MH_ERROR_NONE ||
			mh_net_session_start_frame(session, now - race_origin,
				&error) != MH_ERROR_NONE)
			goto failed;

		if (other != 0 &&
			session->replication.history[other].count >= 3) {
			if (session->replication.history[other]
					.entries[0]
					.vehicle.position[0] !=
				(strcmp(other_name, "Browser") == 0 ? 20
								: 10)) {
				fputs("Remote vehicle position mismatch\n",
					stderr);
				return -1;
			}

			saw_snapshot = 1;
		}
	}

	if (session->phase == MH_NET_RACING) {
		if (!saw_race) {
			saw_race = 1;
			racing_since = now;
		}

		if (!returned && saw_snapshot &&
			now - racing_since >= MIXED_RACE_MS) {
			if (mh_net_session_return_lobby(session, &error) !=
				MH_ERROR_NONE)
				goto failed;

			returned = 1;
		}
	}

	if (mh_network_flush(network, &issue, &error) != MH_ERROR_NONE)
		goto failed;

	if (issue.reason.code != MH_ERROR_NONE) {
		error = issue.reason;
		goto failed;
	}

	return returned && saw_race && saw_snapshot &&
		session->phase == MH_NET_LOBBY && present == 2;

failed:
	fprintf(stderr, "%s: %s\n", driver_name, error.message);

	return -1;
}

void
mh_mixed_client_close(void)
{
	mh_network_close(network);
	network = NULL;
}

#ifdef __EMSCRIPTEN__
int
main(void)
{
	return 0;
}
#else
static volatile sig_atomic_t stopping;

static void
stop(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}

int
main(int argc, char *argv[])
{
	unsigned long port;
	int result, verified;

	if (argc != 3 || !mh_arguments_unsigned(argv[2], 65535, &port) ||
		!mh_mixed_client_open(argv[1], (unsigned int)port))
		return 1;

	signal(SIGTERM, stop);
	verified = 0;
	result = 0;

	while (!stopping && mh_network_now(network) < MIXED_TIMEOUT_MS) {
		result = mh_mixed_client_step();

		if (result < 0)
			break;

		if (result == 1 && !verified) {
			puts("CLIENT VERIFIED: mixed lobby, race snapshots and return");
			fflush(stdout);
			verified = 1;
		}

		mh_clock_source_delay(mh_system_clock_source(), 1, NULL);
	}

	mh_mixed_client_close();

	return verified && result >= 0 ? 0 : 1;
}
#endif
