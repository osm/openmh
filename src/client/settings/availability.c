#include "client/settings/availability.h"
#include "client/settings/render_choice.h"
#include "engine/audio/race_profile.h"
#include "engine/settings/config.h"

void
mh_settings_apply_availability(struct mh_settings *settings)
{
	if (!mh_render_choice_available(settings->renderer))
		settings->renderer = mh_render_choice_default();

	if (!mh_race_audio_available(settings->av.race_audio))
		settings->av.race_audio = mh_race_audio_default();

	if (!mh_screenshot_format_available(settings->av.screenshot_format))
		settings->av.screenshot_format = mh_screenshot_format_default();
}
