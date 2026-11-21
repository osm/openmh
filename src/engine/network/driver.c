#include <string.h>

#include "engine/core/text.h"
#include "engine/network/driver.h"

int
mh_net_driver_text_valid(const struct mh_net_profile *profile)
{
	return mh_text_display_valid(profile->name, sizeof(profile->name)) &&
		mh_text_display_valid(profile->team, sizeof(profile->team)) &&
		mh_text_display_valid(profile->car, sizeof(profile->car)) &&
		mh_text_display_valid(profile->horn, sizeof(profile->horn)) &&
		strchr(profile->name, ':') == NULL &&
		!mh_ascii_equal(profile->name, "console");
}

void
mh_net_driver_edit(struct mh_net_profile *profile,
	const struct mh_net_profile *incoming, int allow_horn)
{
	unsigned int i;

	memcpy(profile->name, incoming->name, sizeof(profile->name));
	memcpy(profile->team, incoming->team, sizeof(profile->team));
	memcpy(profile->car, incoming->car, sizeof(profile->car));

	if (allow_horn)
		memcpy(profile->horn, incoming->horn, sizeof(profile->horn));

	profile->update_rate = incoming->update_rate;
	profile->gear_mode = incoming->gear_mode;
	profile->control_type = incoming->control_type;
	profile->flags = (profile->flags & ~MH_NET_FLAG_RECORDING) |
		(incoming->flags & MH_NET_FLAG_RECORDING);

	for (i = MH_NET_PROFILE_COLOR; i <= MH_NET_PROFILE_CAR_COLOR3; i++)
		profile->values[i] = incoming->values[i];
}

enum mh_error_code
mh_net_driver_chat(const char *name, const char *text, char *output,
	size_t capacity, const char **body, struct mh_error *error_out)
{
	static const char separator[] = ": ";
	size_t length, prefix;
	const char *content;

	if (!mh_text_display_valid(name, MH_NET_NAME_BYTES) ||
		!mh_text_display_valid(text, capacity))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driver chat text");

	if (name[0] == 0)
		name = "Driver";

	length = strlen(name);
	content = text;

	if (strncmp(text, name, length) == 0) {
		if (text[length] == ' ') {
			strcpy(output, text);

			if (body != NULL)
				*body = "";

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (strncmp(text + length, separator, sizeof(separator) - 1) ==
			0)
			content += length + sizeof(separator) - 1;
	}

	prefix = length + sizeof(separator);

	if (prefix > capacity || strlen(content) > capacity - prefix)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "driver chat too long");

	strcpy(output, name);
	strcat(output, separator);
	strcat(output, content);

	if (body != NULL)
		*body = content;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
