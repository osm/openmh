#include "server/session/host_internal.h"
#include "server/session/score_clear.h"

enum mh_error_code
mh_server_host_clear_scores(
	struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_net_message roster;
	enum mh_server_score_clear result;
	const char *text;
	unsigned int i, recipients;
	enum mh_error_code status;

	if (host->race.active)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	result = mh_server_score_clear(&host->lobby);

	if (result == MH_SERVER_SCORE_UNCHANGED)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < host->lobby.roster.player_count; i++) {
		host->race.results[i].lap = 0;
		host->race.results[i].position = 0;
	}

	text = result == MH_SERVER_SCORE_FORCED
		? "Score and gridstats cleared..."
		: "Score and gridstats cleared by majority vote...";
	recipients = mh_server_host_members(host, MH_NET_FLAG_LOBBY);

	/* The forced operator path returns its console line to the caller. */
	if (result == MH_SERVER_SCORE_MAJORITY)
		mh_server_event_log(host->event_logger, text, NULL);

	status = mh_server_host_notice(
		host, recipients, MH_NET_CHAT, text, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_server_lobby_roster(&host->lobby, &roster);

	return mh_server_host_multicast(
		host, recipients, &roster, 1, error_out);
}
