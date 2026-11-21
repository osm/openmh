#ifndef MH_CLIENT_SESSION_NETWORK_RACE_H
#define MH_CLIENT_SESSION_NETWORK_RACE_H

#include "client/race/race.h"
#include "client/session/demo_recording.h"
#include "client/session/network_setup.h"
#include "engine/core/error.h"
#include "engine/network/network_session.h"

#define MH_NETWORK_RACE_PATH_SIZE 1024

struct mh_user_data;

struct mh_network_race {
	struct mh_network		*network;
	struct mh_net_race		*race;
	struct mh_race_context		*race_context;
	struct mh_race_session_hooks	race_hooks;
	struct mh_chat_config		chat_config;
	struct mh_messages		messages;
	unsigned long			chat_revision;
	int				has_messages;

	/* Individual or host-wide return to lobby/setup ended the race. */
	int				returned_to_lobby;

	/* Owned; end before freeing the field/assets. */
	struct mh_demo_recording	*recording;
	char				recording_path[MH_NETWORK_RACE_PATH_SIZE];
	unsigned int			recording_notices;
};

/*
 * Native CLI start policy: expected active lobby participants including host,
 * no pending admissions. The original start barrier still follows loading.
 */
int	mh_network_race_ready(const struct mh_net_session *session,
	unsigned int players);

/*
 * Borrow all objects; keep this controller at a stable address as well.
 * The race loop and offline network checks use the same frame callback.
 */
enum mh_error_code	mh_network_race_init(struct mh_network *network,
	struct mh_net_race *race, struct mh_race_context *context,
	struct mh_network_race *output, struct mh_error *error_out);

/*
 * After init/history clear and before first frame. The session's local flag
 * enables /record. File/name failures notify once and permit racing; only
 * invalid controller/message errors propagate. NULL directory uses user data.
 */
enum mh_error_code	mh_network_race_record(
	struct mh_network_race *controller, const struct mh_user_data *data,
	const char *asset_root, const char *directory, const char *command,
	struct mh_error *error_out);

/*
 * Consume the take before history import/assets release. save=0 aborts;
 * save=1 publishes playable takes and reports failures without losing lobby.
 * Safe with no recorder. Also use for every error/quit cleanup path.
 */
enum mh_error_code	mh_network_race_record_end(
	struct mh_network_race *controller, int save,
	struct mh_error *error_out);

#endif
