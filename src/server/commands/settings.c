#include <stdio.h>
#include <string.h>

#include "engine/core/text.h"
#include "server/commands/settings.h"

#define NOTICE_COLOR 0xe06820UL
#define TRACK_NOT_FOUND "Can't find track: "

static struct mh_server_command_reply *
setting_reply(const struct mh_server_lobby *lobby, unsigned int player,
	int private_reply, struct mh_server_command_output *output)
{
	struct mh_server_command_reply *reply;
	unsigned int i;

	reply = &output->replies[output->count++];
	reply->color = NOTICE_COLOR;
	reply->recipients = 1U << player;

	if (private_reply)
		return reply;

	reply->log = 1;
	reply->recipients = 0;

	for (i = 1; i < lobby->roster.player_count; i++)
		if (lobby->roster.players[i].flags & MH_NET_FLAG_LOBBY)
			reply->recipients |= 1U << i;

	return reply;
}

static void
step_track(struct mh_server_lobby *lobby, const struct mh_server_config *config,
	int up)
{
	unsigned int i;

	for (i = 0; i < config->track_count; i++) {
		if (!mh_ascii_equal(
			lobby->roster.track, config->tracks[i].name))
			continue;

		if (up)
			i = (i + 1) % config->track_count;
		else
			i = i == 0 ? config->track_count - 1 : i - 1;

		strcpy(lobby->roster.track, config->tracks[i].name);

		return;
	}
}

static int
select_track(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, const char *prefix)
{
	unsigned int i;

	/* First case-insensitive prefix match in configured order. */
	for (i = 0; i < config->track_count; i++) {
		if (!mh_ascii_equal_n(
			prefix, config->tracks[i].name, strlen(prefix)))
			continue;

		strcpy(lobby->roster.track, config->tracks[i].name);

		return 1;
	}

	return 0;
}

enum mh_error_code
mh_server_track_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned int player,
	const char *command, const char *arguments,
	struct mh_server_command_output *output, struct mh_error *error_out)
{
	struct mh_server_command_reply *reply;

	if (config->lock_track) {
		reply = setting_reply(lobby, player, 1, output);
		strcpy(reply->text, "Track locked!");
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mh_ascii_equal(command, "/track")) {
		if (!select_track(lobby, config, arguments)) {
			reply = setting_reply(lobby, player, 1, output);
			sprintf(reply->text, TRACK_NOT_FOUND "%.*s",
				(int)(sizeof(reply->text) -
					sizeof(TRACK_NOT_FOUND)),
				arguments);
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	} else
		step_track(lobby, config, mh_ascii_equal(command, "/trackup"));

	reply = setting_reply(lobby, player, 0, output);
	sprintf(reply->text, "%s sets track to: %s",
		lobby->roster.players[player].name, lobby->roster.track);
	output->refresh_roster = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_mode_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned int player,
	struct mh_server_command_output *output, struct mh_error *error_out)
{
	struct mh_server_command_reply *reply;

	reply = setting_reply(lobby, player, config->lock_mode, output);

	if (config->lock_mode) {
		strcpy(reply->text, "Gamemode locked!");
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	lobby->roster.mode = lobby->roster.mode == MH_SERVER_NORMAL
		? MH_SERVER_ELIMINATION
		: MH_SERVER_NORMAL;
	sprintf(reply->text, "%s sets gamemode to: %s",
		lobby->roster.players[player].name,
		lobby->roster.mode == MH_SERVER_NORMAL ? "Normal"
						: "Elimination");
	output->refresh_roster = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_grid_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned int player,
	struct mh_server_command_output *output, struct mh_error *error_out)
{
	static const char *const names[] = {"Random", "LastRace", "ScoreNormal",
		"ScoreReverse", "TeamRandom", "BestLap"};

	struct mh_server_command_reply *reply;

	reply = setting_reply(lobby, player, config->lock_grid, output);

	if (config->lock_grid) {
		strcpy(reply->text, "Gridmode locked!");
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (lobby->grid >= MH_SERVER_GRID_RANDOM &&
		lobby->grid < MH_SERVER_GRID_BEST_LAP)
		lobby->grid = (enum mh_server_grid)(lobby->grid + 1);
	else
		lobby->grid = MH_SERVER_GRID_RANDOM;

	sprintf(reply->text, "%s sets gridmode to: %s",
		lobby->roster.players[player].name, names[lobby->grid]);

	/* Grid policy is server-owned; it does not refresh the wire roster. */

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
