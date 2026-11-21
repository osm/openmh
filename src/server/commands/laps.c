#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/game/lap_presets.h"
#include "engine/game/race_limits.h"
#include "server/commands/laps.h"

#define NOTICE_COLOR 0xe06820UL

/* Decimal prefix, sign and low 32 bits; failed scan retains laps. */
static unsigned int
read_laps(const char *text, unsigned int laps)
{
	unsigned long value;
	int negative;

	while (*text == ' ' || (*text >= '\t' && *text <= '\r'))
		text++;

	negative = *text == '-';

	if (*text == '+' || *text == '-')
		text++;

	value = laps;

	if (*text >= '0' && *text <= '9') {
		value = 0;

		while (*text >= '0' && *text <= '9') {
			value = (value * 10 + (unsigned long)(*text - '0')) &
				MH_U32_MAX;
			text++;
		}

		if (negative)
			value = (0UL - value) & MH_U32_MAX;
	}

	if (value < MH_RACE_MIN_LAPS)
		value = MH_RACE_MIN_LAPS;

	if (value > MH_RACE_MAX_LAPS)
		value = MH_RACE_MAX_LAPS;

	return (unsigned int)value;
}

enum mh_error_code
mh_server_lap_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned int player,
	const char *command, const char *arguments,
	struct mh_server_command_output *output, struct mh_error *error_out)
{
	struct mh_server_command_reply *reply;
	unsigned int i, laps;

	reply = &output->replies[output->count++];
	reply->color = NOTICE_COLOR;

	if (config->lock_laps) {
		reply->recipients = 1U << player;
		strcpy(reply->text, "Laps locked!");
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	reply->log = 1;
	laps = lobby->roster.laps;

	if (mh_ascii_equal(command, "/laps"))
		laps = read_laps(arguments, laps);
	else
		laps = (unsigned int)mh_lobby_lap_step(
			laps, mh_ascii_equal(command, "/lapup"));

	reply->recipients = 0;

	for (i = 1; i < lobby->roster.player_count; i++)
		if (lobby->roster.players[i].flags & MH_NET_FLAG_LOBBY)
			reply->recipients |= 1U << i;

	sprintf(reply->text, "%s sets laps to: %u",
		lobby->roster.players[player].name, laps);
	lobby->roster.laps = laps;
	output->refresh_roster = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
