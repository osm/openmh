#include <string.h>

#include "engine/audio/horn.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/game/race_limits.h"
#include "engine/ui/lobby_settings.h"

static enum mh_error_code
restore_selection(const struct mh_settings_names *names, const char *name,
	unsigned int *index, struct mh_error *error_out)
{
	unsigned int i, found;

	if (names->names == NULL || names->count == 0 ||
		names->count > MH_SETTINGS_CATALOG_MAX ||
		*index >= names->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby return catalog");

	found = names->count;

	for (i = 0; i < names->count; i++) {
		if (!mh_text_field_valid(
			names->names[i], MH_SETTINGS_CATALOG_NAME_BYTES))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby return catalog name");

		if (found == names->count &&
			mh_ascii_equal(names->names[i], name))
			found = i;
	}

	if (found != names->count)
		*index = found;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lobby_settings_profile(const struct mh_net_session *session,
	const struct mh_lobby_settings_catalog *catalog, unsigned long *random,
	struct mh_settings *settings, struct mh_error *error_out)
{
	struct mh_settings next;
	const struct mh_net_profile *profile;
	enum mh_error_code status;
	unsigned long seed;
	unsigned int i;

	if (session == NULL || catalog == NULL || random == NULL ||
		settings == NULL ||
		(session->host != 0 && session->host != 1) ||
		session->player > mh_net_session_capacity(session) ||
		*random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby settings return");

	if (!session->host &&
		session->player == mh_net_session_capacity(session))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if ((session->host && session->player != 0) ||
		(!session->host && session->player == 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby return player");

	profile = &session->config.profile;

	if (!mh_text_field_valid(profile->name, 16) ||
		!mh_text_field_valid(profile->team, 5) ||
		!mh_text_field_valid(profile->car, 16) ||
		!mh_text_field_valid(profile->horn, 16) ||
		profile->update_rate < MH_SETTINGS_RATE_MIN ||
		profile->update_rate > MH_SETTINGS_RATE_MAX ||
		session->config.prediction.minimum > MH_NET_PREDICTION_LIMIT ||
		session->config.prediction.maximum > MH_NET_PREDICTION_LIMIT ||
		profile->gear_mode > 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby return profile or setup");

	for (i = 0; i < 4; i++)
		if (profile->values[i] > 0xffffffUL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby return color");

	next = *settings;
	seed = *random;
	status = restore_selection(
		&catalog->cars, profile->car, &next.car, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_horn_profile_find(catalog->horns, profile->horn,
			&seed, &next.horn, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	strcpy(next.player_name, profile->name);
	strcpy(next.team_name, profile->team);
	next.player_color = profile->values[MH_NET_PROFILE_COLOR];

	for (i = 0; i < 3; i++)
		next.car_colors[i] =
			profile->values[MH_NET_PROFILE_CAR_COLOR1 + i];

	next.automatic = profile->gear_mode;
	next.tcp_rate = profile->update_rate;
	next.prediction = session->config.prediction;
	*settings = next;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lobby_settings_return(const struct mh_net_session *session,
	const struct mh_lobby_settings_catalog *catalog, unsigned long *random,
	struct mh_settings *settings, struct mh_error *error_out)
{
	struct mh_settings next;
	const char *track;
	unsigned long seed;
	unsigned int laps;
	enum mh_error_code status;

	if (settings == NULL || random == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing lobby settings");

	next = *settings;
	seed = *random;
	status = mh_lobby_settings_profile(
		session, catalog, &seed, &next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!session->host &&
		session->player == mh_net_session_capacity(session))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/*
	 * Before the first roster, retain the initial setup as native menus do.
	 */
	track = session->host || session->roster.player_count == 0
		? session->config.track
		: session->roster.track;
	laps = session->host || session->roster.player_count == 0
		? session->config.laps
		: session->roster.laps;

	if (!mh_text_field_valid(track, MH_NET_NAME_BYTES) ||
		laps < MH_RACE_MIN_LAPS || laps > MH_RACE_MAX_LAPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby return setup");

	status = restore_selection(
		&catalog->tracks, track, &next.track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.laps = laps;
	*settings = next;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
