#include <string.h>

#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "engine/game/race_limits.h"
#include "engine/settings/config_internal.h"
#include "engine/settings/menu_return.h"

static int
text_valid(const char *text, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (text[i] == 0)
			return 1;

		if ((unsigned char)text[i] < 32)
			return 0;
	}

	return 0;
}

static enum mh_error_code
restore_pause_options(struct mh_settings *next,
	const struct mh_settings *snapshot, struct mh_error *error_out)
{
	if (snapshot->av.car_view > MH_CAR_VIEW_OUTSIDE ||
		snapshot->av.effects_volume > MH_AV_VOLUME_MAX ||
		snapshot->av.music_volume > MH_AV_VOLUME_MAX ||
		snapshot->av.car_shading > MH_AV_SHADING_GLENZ ||
		(snapshot->av.perspective != MH_AV_PERSPECTIVE_GOOD &&
			snapshot->av.perspective != MH_AV_PERSPECTIVE_NORMAL) ||
		!(snapshot->av.view_distance >= MH_AV_VIEW_DISTANCE_MIN &&
			snapshot->av.view_distance <= MH_AV_VIEW_DISTANCE_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid returning pause options");

	next->av.car_view = snapshot->av.car_view;
	next->av.effects_volume = snapshot->av.effects_volume;
	next->av.music_volume = snapshot->av.music_volume;
	next->av.car_shading = snapshot->av.car_shading;
	next->av.perspective = snapshot->av.perspective;
	next->av.view_distance = snapshot->av.view_distance;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
restore_lobby_preferences(struct mh_settings *next,
	const struct mh_settings *snapshot, struct mh_error *error_out)
{
	unsigned int i;

	if (!text_valid(snapshot->player_name, sizeof(snapshot->player_name)) ||
		!text_valid(snapshot->team_name, sizeof(snapshot->team_name)) ||
		snapshot->player_color > MH_CFG_RGB_MAX ||
		snapshot->car >= MH_SETTINGS_CATALOG_MAX ||
		snapshot->track >= MH_SETTINGS_CATALOG_MAX ||
		snapshot->horn >= MH_SETTINGS_CATALOG_MAX ||
		snapshot->tcp_rate < MH_SETTINGS_RATE_MIN ||
		snapshot->tcp_rate > MH_SETTINGS_RATE_MAX ||
		snapshot->prediction.minimum > MH_NET_PREDICTION_LIMIT ||
		snapshot->prediction.maximum > MH_NET_PREDICTION_LIMIT ||
		snapshot->automatic > 1 || snapshot->laps < MH_RACE_MIN_LAPS ||
		snapshot->laps > MH_RACE_MAX_LAPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid returning lobby preferences");

	for (i = 0; i < MH_SETTINGS_CAR_COLORS; i++) {
		if (snapshot->car_colors[i] > MH_CFG_RGB_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid returning lobby paint");

		next->car_colors[i] = snapshot->car_colors[i];
	}

	strcpy(next->player_name, snapshot->player_name);
	strcpy(next->team_name, snapshot->team_name);
	next->player_color = snapshot->player_color;
	next->car = snapshot->car;
	next->track = snapshot->track;
	next->horn = snapshot->horn;
	next->automatic = snapshot->automatic;
	next->laps = snapshot->laps;
	next->tcp_rate = snapshot->tcp_rate;
	next->prediction = snapshot->prediction;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_settings_restore_menu(const struct mh_settings *loaded,
	const struct mh_settings *snapshot, unsigned int mode,
	struct mh_settings *output, struct mh_error *error_out)
{
	struct mh_settings next;
	enum mh_error_code status;

	if (loaded == NULL || output == NULL || mode > MH_GAME_DEMO)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid frontend settings return");

	next = *loaded;

	if (snapshot != NULL) {
		status = restore_pause_options(&next, snapshot, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (mode == MH_GAME_NETWORK) {
			status = restore_lobby_preferences(
				&next, snapshot, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
