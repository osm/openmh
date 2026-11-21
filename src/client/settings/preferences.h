#ifndef MH_CLIENT_SETTINGS_PREFERENCES_H
#define MH_CLIENT_SETTINGS_PREFERENCES_H

#include "engine/settings/config.h"

/*
 * Named, typed preferences; no disk-format or host ABI is exposed to callers.
 */
enum mh_preference_type {
	MH_PREFERENCE_UINT,
	MH_PREFERENCE_ULONG,
	MH_PREFERENCE_FLOAT,
	MH_PREFERENCE_TEXT
};

enum mh_preference_feature {
	MH_PREFERENCE_COMMON,
	MH_PREFERENCE_GRAPHICS,
	MH_PREFERENCE_PRESENTATION,
	MH_PREFERENCE_WINDOW,
	MH_PREFERENCE_SCREENSHOT,
	MH_PREFERENCE_MENUS,
	MH_PREFERENCE_RECORDING,
	MH_PREFERENCE_LEGACY_NETWORK,
	MH_PREFERENCE_NETWORK,
	MH_PREFERENCE_MUSIC
};

struct mh_preference {
	char				name[48];
	enum mh_preference_type		type;
	size_t				offset, capacity;
	double				minimum, maximum, step;

	/*
	 * Numeric choices separated by |, each value:label. Empty uses range.
	 */
	const char			*choices;
	enum mh_preference_feature	feature;
	unsigned int			graphics_row;
	int				one_based, volume, custom_detail;
};

/* Course entries follow fixed fields, using installed one-based track IDs. */
unsigned int		mh_preference_count(unsigned int courses);
int			mh_preference_describe(unsigned int index,
	unsigned int courses, struct mh_preference *output);
double			mh_preference_number(const struct mh_settings *settings,
	const struct mh_preference *field);
const char	       *mh_preference_text(const struct mh_settings *settings,
	const struct mh_preference *field);
enum mh_error_code	mh_preference_set(struct mh_settings *settings,
	const struct mh_preference *field, double number, const char *text,
	struct mh_error *error_out);

#endif
