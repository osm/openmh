#ifndef MH_CLIENT_APPLICATION_PREFERENCES_H
#define MH_CLIENT_APPLICATION_PREFERENCES_H

#include "client/settings/preferences.h"
#include "engine/settings/control_layout.h"

#define MH_PREFERENCE_MOUSE_SPEED_MIN .01
#define MH_PREFERENCE_MOUSE_SPEED_MAX 10

struct mh_application;

struct mh_preference_update {
	unsigned int	index;
	double		number;
	const char	*text;
};

const struct mh_settings	*mh_application_preferences(
	const struct mh_application *app);

/*
 * Empty reason means supported. Availability describes the selected renderer.
 */
const char	*mh_application_preference_reason(
	const struct mh_application *app, const struct mh_settings *settings,
	const struct mh_preference *field);

/* All values are validated before publication. Idle/disconnected only. */
enum mh_error_code		 mh_application_update_preferences(
	struct mh_application *app, const struct mh_preference_update *updates,
	unsigned int count, struct mh_error *error_out);
const struct mh_control_layout	*mh_application_control_profile(
	const struct mh_application *app, unsigned int profile);
int				 mh_application_control_action_available(
	const struct mh_application *app, unsigned int action);
int				 mh_application_control_feedback_available(
	const struct mh_application *app);
int				 mh_application_control_scan_available(
	unsigned long scan);
enum mh_error_code		 mh_application_update_control_profile(
	struct mh_application *app, unsigned int profile,
	const struct mh_control_layout *layout, struct mh_error *error_out);

#endif
