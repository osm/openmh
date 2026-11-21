#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/settings/control_catalog.h"

void
mh_control_catalog_free(struct mh_control_catalog *catalog)
{
	if (catalog == NULL)
		return;

	free(catalog->profiles);
	memset(catalog, 0, sizeof(*catalog));
}

enum mh_error_code
mh_control_catalog_read(const struct mh_control_source *sources,
	unsigned int count, const char *selection, const char *const *key_names,
	struct mh_control_catalog *output, struct mh_error *error_out)
{
	struct mh_control_catalog next;
	enum mh_error_code status;
	unsigned int i;

	if (sources == NULL || selection == NULL || output == NULL ||
		count == 0 || count > MH_CONTROL_PROFILES ||
		output->profiles != NULL || output->count != 0 ||
		output->selected != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control catalog input");

	for (i = 0; i < MH_CONTROL_SELECTION_BYTES && selection[i] != 0; i++) {
	}

	if (i == MH_CONTROL_SELECTION_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated control selection");

	for (i = 0; i < count; i++) {
		if ((sources[i].present != 0 && sources[i].present != 1) ||
			(!sources[i].present &&
				(sources[i].text != NULL ||
					sources[i].size != 0)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid control catalog source");
	}

	memset(&next, 0, sizeof(next));
	next.profiles = (struct mh_control_profile *)calloc(
		count, sizeof(*next.profiles));

	if (next.profiles == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading control catalog");

	next.count = count;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < count && status == MH_ERROR_NONE; i++) {
		status = mh_control_layout_read_at(sources[i].present
				? sources[i].text
				: (const unsigned char *)"",
			sources[i].size, key_names, i + 1,
			&next.profiles[i].layout, error_out);
		next.profiles[i].dirty = sources[i].present ? 0 : 1;

		if (status == MH_ERROR_NONE &&
			mh_ascii_equal(next.profiles[i].layout.name, selection))
			next.selected = i;
	}

	if (status != MH_ERROR_NONE) {
		mh_control_catalog_free(&next);
		return status;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
