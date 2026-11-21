#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/stdin_ready.h"
#include "backends/clock/system_provider.h"
#include "backends/navigation/provider.h"
#include "backends/network/posix_provider.h"
#include "backends/network/udp_transport_provider.h"
#include "backends/network/windows_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/input/control_type.h"
#include "engine/network/network_address.h"
#include "engine/network/network_session.h"
#include "engine/simulation/route_curve.h"
#include "server/config/load.h"
#include "server/courses/courses.h"

/* Real UDP/session test driver, with authored motion instead of car physics. */
#define KEY_MILLISECONDS 100.0
#define INTRODUCTION_MS 12500.0
#define RUN_TIMEOUT_MS 240000.0

struct driver {
	struct mh_network		*network;
	struct mh_server_config		config;
	struct mh_server_courses	courses;
	const struct mh_route		*route;
	unsigned int	rounds, seen, reported_phase, reported_members;
	unsigned int	reported_ready, reported_passing, reported_racing;
	unsigned long			statistics_revision;
	unsigned long			chat_revision;
	unsigned long			roster_revision;
	double				loaded_ms;
};

static enum mh_error_code
read_command(struct driver *driver, struct mh_error *error_out)
{
	struct mh_net_session *session;
	char line[128], text[160];
	int available;

	available = mh_test_stdin_ready();

	if (available < 0)
		return mh_error_set(
			error_out, MH_ERROR_IO, "driver stdin failed");

	if (!available)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	session = mh_network_session(driver->network);

	if (fgets(line, sizeof(line), stdin) == NULL)
		return mh_net_session_leave(session, error_out);

	line[strcspn(line, "\r\n")] = 0;

	if (!strcmp(line, "quit"))
		return mh_net_session_leave(session, error_out);

	if (!strcmp(line, "return"))
		return mh_net_session_return_lobby(session, error_out);

	if (!strncmp(line, "chat ", 5)) {
		sprintf(text, "%s: %s", session->config.profile.name, line + 5);
		return mh_net_session_chat(
			session, text, 0x123456UL, error_out);
	}

	if (!strcmp(line, "ready") || !strcmp(line, "notready")) {
		sprintf(text, "%s: /%s", session->config.profile.name, line);
		return mh_net_session_chat(session, text, 0, error_out);
	}

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "unknown driver command");
}

static enum mh_error_code
load_race(struct driver *driver, struct mh_error *error_out)
{
	struct mh_net_session *session;
	double positions[MH_NET_WIRE_PLAYERS][3];
	unsigned int i, j;
	enum mh_error_code status;

	session = mh_network_session(driver->network);
	driver->route = NULL;

	for (i = 0; i < driver->courses.count; i++)
		if (mh_ascii_equal(driver->config.tracks[i].name,
			session->roster.track))
			driver->route = &driver->courses.routes[i];

	if (driver->route == NULL)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "driver course missing");

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		for (j = 0; j < 3; j++)
			positions[i][j] =
				driver->route->motion.keys[0].value[j];

	status = mh_net_session_configure_replication(
		session, 180, (const double (*)[3])positions, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	driver->loaded_ms = mh_network_now(driver->network);
	driver->seen = 0;
	driver->rounds++;
	printf("GRID %u %u %u\n", session->player, driver->rounds,
		session->roster.players[session->player].value_a);
	printf("LOAD %u %u %s %lu\n", session->player, driver->rounds,
		session->roster.track,
		(unsigned long)driver->route->motion.key_count);

	return mh_net_session_loaded(session, driver->loaded_ms, error_out);
}

static enum mh_error_code
publish_position(struct driver *driver, struct mh_error *error_out)
{
	struct mh_net_session *session;
	struct mh_net_vehicle vehicle;
	const struct mh_scene_key *keys;
	float sampled[9];
	double game_ms, elapsed, frame;
	size_t index, count;
	unsigned int i;
	enum mh_error_code status;

	session = mh_network_session(driver->network);
	game_ms = mh_network_now(driver->network) - driver->loaded_ms;
	status = mh_net_session_start_frame(session, game_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	elapsed = game_ms - session->start.started_ms - INTRODUCTION_MS;
	index = 0;
	count = driver->route->motion.key_count - 1;

	/*
	 * Elimination has one moving leader; the other real clients keep
	 * sending.
	 */
	if (session->start.started_ms != 0 && elapsed > 0 &&
		(session->roster.mode == MH_NET_MODE_NORMAL ||
			session->player == 1))
		index = (size_t)(elapsed / KEY_MILLISECONDS) % count;

	keys = driver->route->motion.keys;
	frame = keys[index].time +
		(keys[index + 1].time - keys[index].time) * .05;
	status =
		mh_route_curve_sample(driver->route, frame, sampled, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&vehicle, 0, sizeof(vehicle));

	for (i = 0; i < 3; i++)
		vehicle.position[i] = sampled[i];

	vehicle.rpm = 2000;

	return mh_net_session_publish(
		session, &vehicle, session->player, error_out);
}

static enum mh_error_code
observe_session(void *context, struct mh_error *error_out)
{
	struct driver *driver;
	struct mh_net_session *session;
	const struct mh_net_statistics_row *row;
	unsigned int i, members, ready, passing, racing;
	unsigned long lines;

	driver = (struct driver *)context;
	session = mh_network_session(driver->network);
	lines = session->chat_revision - driver->chat_revision;

	if (lines > session->chat_count)
		lines = session->chat_count;

	for (i = session->chat_count - (unsigned int)lines;
		i < session->chat_count; i++)
		printf("CHAT %lu %s\n", session->chat[i].color,
			session->chat[i].text);

	driver->chat_revision = session->chat_revision;

	if (session->phase == MH_NET_CONNECTING ||
		session->phase == MH_NET_JOINING)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (session->roster.players[0].flags & MH_NET_FLAG_PRESENT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"server appeared as driver");

	if (driver->roster_revision != session->roster_revision) {
		if (driver->roster_revision == 0)
			printf("PORT %u\n", mh_network_port(driver->network));

		driver->roster_revision = session->roster_revision;
		printf("SERVER %u %u %s\n", session->roster.players[0].flags,
			session->replication.status[0].flags,
			session->roster.players[0].name);
		printf("SETTINGS %u %u %s\n", session->roster.laps,
			session->roster.mode, session->roster.track);

		for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
			const struct mh_net_profile *profile;

			profile = &session->roster.players[i];
			printf("SCORE %u %lu %lu %u %u\n", i,
				profile->values[MH_NET_PROFILE_RACES],
				profile->values[MH_NET_PROFILE_POINTS],
				profile->value_a, profile->value_b);
		}
	}

	members = ready = passing = racing = 0;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (session->roster.players[i].flags & MH_NET_FLAG_PRESENT)
			members |= 1U << i;

		if (session->roster.players[i].flags & MH_NET_FLAG_READY)
			ready |= 1U << i;

		if (session->roster.players[i].flags & MH_NET_FLAG_PASS)
			passing |= 1U << i;

		if (session->roster.players[i].flags & MH_NET_FLAG_RACING)
			racing |= 1U << i;

		if (session->replication.history[i].count)
			driver->seen |= 1U << i;
	}

	if (driver->reported_phase != (unsigned int)session->phase ||
		driver->reported_members != members ||
		driver->reported_ready != ready ||
		driver->reported_passing != passing ||
		driver->reported_racing != racing) {
		printf("STATE %u %u %u %u %u %u\n", session->player,
			(unsigned int)session->phase, members, ready, passing,
			racing);
		driver->reported_phase = (unsigned int)session->phase;
		driver->reported_members = members;
		driver->reported_ready = ready;
		driver->reported_passing = passing;
		driver->reported_racing = racing;
	}

	if (driver->statistics_revision != session->statistics_revision) {
		const struct mh_net_profile *profile;

		driver->statistics_revision = session->statistics_revision;
		profile = &session->roster.players[session->player];
		printf("RESULT %u %lu %u %lu %lu %s\n", session->player,
			driver->statistics_revision, driver->seen,
			profile->values[MH_NET_PROFILE_RACES],
			profile->values[MH_NET_PROFILE_POINTS],
			session->roster.track);

		for (i = 0; i < MH_NET_STATISTICS_ROWS; i++) {
			row = &session->last_race.rows[i];
			printf("ROW %lu %u %u %lu %lu %s\n",
				driver->statistics_revision, row->position,
				row->top_speed_kmh, row->best_lap_ms,
				row->race_ms, row->name);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
run(struct driver *driver, struct mh_error *error_out)
{
	struct mh_net_session *session;
	struct mh_network_issue issue;
	double started;
	enum mh_error_code status;

	session = mh_network_session(driver->network);
	started = mh_network_now(driver->network);

	while (session->phase != MH_NET_CLOSED) {
		status = read_command(driver, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (session->phase == MH_NET_LOADING)
			status = load_race(driver, error_out);

		if (status == MH_ERROR_NONE &&
			(session->phase == MH_NET_WAITING ||
				session->phase == MH_NET_RACING))
			status = publish_position(driver, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_network_drain_observed(driver->network, 64, &issue,
			observe_session, driver, error_out);

		if (session->phase == MH_NET_REJECTED)
			return mh_error_set(
				error_out, MH_ERROR_FAILED, session->rejection);

		if (status != MH_ERROR_NONE)
			return status;

		if (issue.reason.code != MH_ERROR_NONE)
			return mh_error_forward(error_out, &issue.reason);

		status = observe_session(driver, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (mh_network_now(driver->network) - started > RUN_TIMEOUT_MS)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"driver race timeout");

		status = mh_clock_source_delay(
			mh_system_clock_source(), 5, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_network_flush(driver->network, &issue, error_out);
}

int
main(int argc, char *argv[])
{
	struct driver driver;
	struct mh_udp_transport_config transport_config;
	struct mh_transport_provider provider;
	struct mh_net_session_config config;
	struct mh_error error;
	unsigned long port;
	unsigned int failed_track;
	size_t line;
	char *end;
	enum mh_error_code status;

	if (argc < 5 || argc > 7 || strlen(argv[2]) >= MH_NET_NAME_BYTES ||
		(argc >= 6 && strlen(argv[5]) >= MH_NET_TEAM_BYTES) ||
		(argc == 7 && strlen(argv[6]) >= MH_NET_PASSWORD_BYTES))
		return 2;

	port = strtoul(argv[1], &end, 10);

	if (*argv[1] == 0 || *end != 0 || port == 0 || port > MH_U16_MAX)
		return 2;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stdin, NULL, _IONBF, 0);
	memset(&driver, 0, sizeof(driver));
	status = mh_server_config_load(mh_filesystem_resources(), argv[3],
		&driver.config, &line, &error);

	if (status == MH_ERROR_NONE)
		status = mh_server_courses_load(mh_navigation_tbl_routes(),
			mh_filesystem_resources(), argv[4], &driver.config,
			&driver.courses, &failed_track, &error);

	memset(&transport_config, 0, sizeof(transport_config));
	transport_config.clock = mh_system_clock_source();
	transport_config.port_policy = driver.config.network_legacy
		? MH_UDP_PORTS_OFFSET
		: MH_UDP_PORTS_SHARED;
#ifdef _WIN32
	transport_config.resolver = mh_windows_resolver_provider();
#else
	transport_config.resolver = mh_posix_resolver_provider();
#endif

	if (status == MH_ERROR_NONE)
		status = mh_udp_transport_init(
			&provider, &transport_config, &error);

	memset(&config, 0, sizeof(config));
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.simulation_rate = driver.config.network_legacy
		? 0
		: (unsigned int)driver.config.simulation_rate;
	config.provider = MH_NET_PROVIDER_LANINET;
	config.laps = 1;
	config.car_count = 10;
	strcpy(config.track, "Goldbridge");
	strcpy(config.profile.name, argv[2]);

	if (argc >= 6)
		strcpy(config.profile.team, argv[5]);

	if (argc == 7)
		strcpy(config.password, argv[6]);

	config.profile.values[MH_NET_PROFILE_COLOR] = 0x2468acUL;
	strcpy(config.profile.car, "Kobra X3");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 30;
	config.profile.control_type = MH_CONTROL_KEYBOARD;
	config.profile.flags = MH_NET_FLAG_LOBBY;

	if (status == MH_ERROR_NONE)
		status = mh_network_open_address(&provider, 0, "127.0.0.1",
			"127.0.0.1", (unsigned int)port, &config,
			&driver.network, &error);

	if (status == MH_ERROR_NONE)
		status = run(&driver, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_network_close(driver.network);
	mh_server_courses_free(&driver.courses);

	return status != MH_ERROR_NONE;
}
