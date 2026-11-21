#include <stdlib.h>

#include "engine/core/limits.h"
#include "engine/core/simulation_rate.h"
#include "engine/game/race_limits.h"
#include "interfaces/storage/resource.h"
#include "server/config/load.h"

#define CONFIG_BYTE_LIMIT (1024UL * 1024)

enum {
	MAX_WIRE_BYTE = 255
};

enum mh_error_code
mh_server_config_load(const struct mh_resource_source *source, const char *path,
	struct mh_server_config *config, size_t *error_line,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	if (error_line != NULL)
		*error_line = 0;

	status = mh_resource_source_read_bounded(
		source, path, CONFIG_BYTE_LIMIT, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_server_config_parse(config, (const char *)file.data,
		file.size, error_line, error_out);
	free(file.data);

	return status;
}

enum mh_error_code
mh_server_config_validate(
	const struct mh_server_config *config, struct mh_error *error_out)
{
	if (!mh_sim_rate_valid(config->simulation_rate))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"SimulationRate must be an integer from 1 to 4294967295 Hz");

	if (config->port > MH_U16_MAX ||
		config->clients_start_port > MH_U16_MAX ||
		config->clients_end_port > MH_U16_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"server UDP ports must fit 16 bits");

	if (config->max_players == 0 ||
		config->max_players > MH_SERVER_RACERS ||
		config->max_rate == 0 || config->max_rate > MAX_WIRE_BYTE ||
		config->laps < MH_RACE_MIN_LAPS ||
		config->laps > MH_RACE_MAX_LAPS || config->unlocked_cars == 0 ||
		config->unlocked_cars > MAX_WIRE_BYTE)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid server player, rate, lap or unlocked-car limit");

	if (config->min_ready > config->max_players)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"MinReady cannot exceed MaxPlayers");

	if (config->track_count == 0 || config->start_track[0] == '\0')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"server requires Track entries and StartTrack");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
