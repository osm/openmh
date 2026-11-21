#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/game/race_limits.h"
#include "engine/input/control_type.h"
#include "server/logging/race.h"

/* Clamp milliseconds to the largest three-digit minute value. */
#define LOG_TIME_LIMIT_MS 60000000UL

#define MILLISECONDS_PER_MINUTE 60000UL
#define MILLISECONDS_PER_SECOND 1000UL
#define MILLISECONDS_PER_HUNDREDTH 10UL

enum {
	TIME_TEXT_BYTES = 16,
	SECONDS_PER_MINUTE = 60,
	HUNDREDTHS_PER_SECOND = 100
};

static enum mh_error_code
validate_record(const struct mh_net_roster *roster,
	const struct mh_server_result *results, struct mh_error *error_out)
{
	const struct mh_net_profile *profile;
	unsigned int i;

	if (roster == NULL || results == NULL || roster->player_count < 2 ||
		roster->player_count > MH_NET_WIRE_PLAYERS ||
		roster->mode > MH_NET_MODE_ELIMINATION ||
		roster->laps > MH_RACE_MAX_LAPS ||
		memchr(roster->track, 0, sizeof(roster->track)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated race log context");

	for (i = 1; i < roster->player_count; i++) {
		profile = &roster->players[i];

		if (!(profile->flags & MH_NET_FLAG_RACING))
			continue;

		if (memchr(profile->name, 0, sizeof(profile->name)) == NULL ||
			memchr(profile->team, 0, sizeof(profile->team)) ==
				NULL ||
			memchr(profile->car, 0, sizeof(profile->car)) == NULL ||
			results[i].elapsed_ms > MH_U32_MAX ||
			results[i].best_lap_ms > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid dedicated race log driver");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
format_time(unsigned long milliseconds, char text[TIME_TEXT_BYTES])
{
	if (milliseconds >= LOG_TIME_LIMIT_MS)
		milliseconds = LOG_TIME_LIMIT_MS - 1;

	sprintf(text, "%03lu:%02lu.%02lu",
		milliseconds / MILLISECONDS_PER_MINUTE,
		(milliseconds / MILLISECONDS_PER_SECOND) % SECONDS_PER_MINUTE,
		(milliseconds / MILLISECONDS_PER_HUNDREDTH) %
			HUNDREDTHS_PER_SECOND);
}

static char *
append_driver(char *text, unsigned int rank,
	const struct mh_net_profile *profile,
	const struct mh_server_result *result, char *control)
{
	char elapsed[TIME_TEXT_BYTES], best[TIME_TEXT_BYTES];
	unsigned long race_ms, best_ms;

	/* Unclassified input retains the preceding row's letter. */
	if (profile->control_type == MH_CONTROL_KEYBOARD)
		*control = 'K';
	else if (profile->control_type == MH_CONTROL_MOUSE)
		*control = 'M';
	else if (profile->control_type == MH_CONTROL_JOYSTICK)
		*control = 'J';

	/* First-lap rows use elapsed time twice, even if unfinished. */
	if (result->lap <= 1)
		race_ms = best_ms = result->elapsed_ms;
	else {
		race_ms = result->finished ? result->elapsed_ms : 0;
		best_ms = result->best_lap_ms;
	}

	format_time(race_ms, elapsed);
	format_time(best_ms, best);
	sprintf(text, "%u\t%-15s\t%-4s\t%-15s\t%c\t%c\t%s\t%s\t", rank,
		profile->name, profile->team, profile->car,
		profile->gear_mode == 0 ? 'M' : 'A', *control, elapsed, best);

	return text + strlen(text);
}

enum mh_error_code
mh_server_race_record(const struct mh_net_roster *roster,
	const struct mh_server_result results[MH_NET_WIRE_PLAYERS],
	char output[MH_SERVER_RACE_RECORD_BYTES], struct mh_error *error_out)
{
	char record[MH_SERVER_RACE_RECORD_BYTES], *text, control;
	unsigned int rank, player;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing dedicated race log output");

	status = validate_record(roster, results, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Fixed field limits keep the complete twelve-driver record below 1
	 * KiB.
	 */
	sprintf(record, "%s\t%u\t%s\t", roster->track, roster->laps,
		roster->mode == MH_NET_MODE_NORMAL ? "Normal" : "Elimination");
	text = record + strlen(record);
	control = ' ';

	for (rank = 1; rank < MH_NET_WIRE_PLAYERS; rank++) {
		for (player = 1; player < roster->player_count; player++)
			if ((roster->players[player].flags &
				MH_NET_FLAG_RACING) &&
				results[player].position == rank)
				break;

		if (player < roster->player_count)
			text = append_driver(text, rank,
				&roster->players[player], &results[player],
				&control);
		else {
			sprintf(text, "%u\t\t\t\t\t\t\t\t", rank);
			text += strlen(text);
		}
	}

	strcpy(text, "\n");
	strcpy(output, record);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
