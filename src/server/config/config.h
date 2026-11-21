#ifndef MH_SERVER_CONFIG_H
#define MH_SERVER_CONFIG_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/audio/horn.h"
#include "interfaces/catalog/catalog.h"

struct mh_net_protocol;

enum {
	MH_SERVER_NAME_BYTES = 16,
	MH_SERVER_BAN_ADDRESS_BYTES = 64,
	MH_SERVER_PATH_BYTES = 256,
	MH_SERVER_TRACKS = 100,
	MH_SERVER_BANS = 20,
	MH_SERVER_RACERS = 12
};

enum mh_server_grid {
	MH_SERVER_GRID_RANDOM,
	MH_SERVER_GRID_LAST_RACE,
	MH_SERVER_GRID_SCORE_NORMAL,
	MH_SERVER_GRID_SCORE_REVERSE,
	MH_SERVER_GRID_TEAM_RANDOM,
	MH_SERVER_GRID_BEST_LAP
};

enum mh_server_mode {
	MH_SERVER_NORMAL,
	MH_SERVER_ELIMINATION
};

struct mh_server_track {
	char	name[MH_SERVER_NAME_BYTES];
	char	path[MH_SERVER_PATH_BYTES];
};

/* Server configuration units; these are not wire structures. */
struct mh_server_config {
	char			session_name[MH_SERVER_NAME_BYTES];
	char			password[MH_SERVER_NAME_BYTES];
	char			race_log[MH_SERVER_PATH_BYTES];
	char			server_name[MH_SERVER_NAME_BYTES];
	char			start_track[MH_SERVER_NAME_BYTES];

	unsigned long		port;
	unsigned long		clients_start_port;
	unsigned long		clients_end_port;

	unsigned long		max_players;
	unsigned long		min_ready;
	unsigned long		max_rate;
	unsigned long		simulation_rate;
	int			network_legacy;

	unsigned long		end_countdown_seconds;
	unsigned long		max_race_minutes;
	unsigned long		ban_minutes;
	unsigned long		unlocked_cars;
	unsigned long		laps;

	unsigned long		lobby_timeout_seconds;
	unsigned long		race_timeout_seconds;
	unsigned long		start_timeout_seconds;

	enum mh_server_mode	mode;
	enum mh_server_grid	grid;

	int			disable_io;
	int			all_ready;
	int			ghost_reverse;
	int			ghost_all;
	int			step_track;

	int			lock_track;
	int			lock_laps;
	int			lock_mode;
	int			lock_grid;

	unsigned int		car_count, horn_count;
	char			cars[MH_CAR_CATALOG_LIMIT][MH_SERVER_NAME_BYTES];
	char			horns[MH_HORNS][MH_SERVER_NAME_BYTES];

	unsigned int		track_count;
	struct mh_server_track	tracks[MH_SERVER_TRACKS];

	unsigned int		ban_count;
	char			bans[MH_SERVER_BANS][MH_SERVER_BAN_ADDRESS_BYTES];
};

void	mh_server_config_defaults(struct mh_server_config *config);

/*
 * Parse borrowed bytes, starting with original defaults. No filesystem access.
 * error_line is optional (one-based, zero on success). On failure config may
 * contain partial settings and must not be used to start a server.
 */
enum mh_error_code	mh_server_config_parse(struct mh_server_config *config,
	const char *text, size_t size, size_t *error_line,
	struct mh_error *error_out);

const struct mh_net_protocol	*mh_server_config_protocol(
	const struct mh_server_config *config);

#endif
