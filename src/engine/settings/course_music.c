#include <string.h>

#include "engine/core/limits.h"
#include "engine/settings/course_music.h"

unsigned int
mh_settings_track_music(
	const struct mh_settings *settings, const struct mh_track *track)
{
	unsigned int i;

	if (settings == NULL || !track->has_division)
		return track->cd_track;

	for (i = 0; i < settings->course_music_count; i++)
		if (settings->course_music[i].division == track->division)
			return (unsigned int)settings->course_music[i].track;

	return track->cd_track;
}

enum mh_error_code
mh_settings_music_bind(struct mh_settings *settings,
	const struct mh_track *tracks, unsigned int count,
	struct mh_error *error_out)
{
	struct mh_settings_course_music music[MH_SETTINGS_COURSES];
	unsigned int i;

	if (settings == NULL || tracks == NULL || count > MH_SETTINGS_COURSES ||
		settings->course_music_count > MH_SETTINGS_COURSES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid course music catalog");

	memset(music, 0, sizeof(music));

	for (i = 0; i < count; i++) {
		if (!tracks[i].has_division || tracks[i].division > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"course music requires a Division");

		music[i].division = tracks[i].division;
		music[i].track = mh_settings_track_music(settings, &tracks[i]);
	}

	memcpy(settings->course_music, music, sizeof(music));
	settings->course_music_count = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
