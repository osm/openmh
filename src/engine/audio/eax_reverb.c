#include "engine/audio/eax_reverb.h"

/* EAX property 3 (seconds) and property 4. */
#define DECAY_SECONDS 1.0
#define DAMPING .5

enum mh_error_code
mh_eax_reverb_create(unsigned long rate, struct mh_reverb **output,
	struct mh_error *error_out)
{
	struct mh_reverb_settings settings;

	settings.volume = 0;
	settings.decay = DECAY_SECONDS;
	settings.damping = DAMPING;

	return mh_reverb_create(rate, &settings, output, error_out);
}

enum mh_error_code
mh_eax_reverb_update(struct mh_reverb *reverb,
	const struct mh_eax_listener_update *update, struct mh_error *error_out)
{
	struct mh_reverb_settings settings;

	if (reverb == NULL || update == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid EAX reverb request");

	if (!update->reverb_changed)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	settings.volume = update->reverb;
	settings.decay = DECAY_SECONDS;
	settings.damping = DAMPING;

	return mh_reverb_configure(reverb, &settings, error_out);
}
