#include <string.h>

#include "engine/audio/catalog.h"

enum mh_error_code
mh_audio_catalog_collect(
	int (*probe)(void *context, enum mh_audio_profile profile),
	void *context, struct mh_audio_catalog *output,
	struct mh_error *error_out)
{
	struct mh_audio_catalog catalog;
	int available[MH_AUDIO_PROFILE_COUNT];
	int profile;

	if (probe == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio catalog request");

	/* Probe downward and prepend successful descriptions. */
	available[MH_AUDIO_PROFILE_SILENT] = 1;

	for (profile = MH_AUDIO_PROFILE_A3D20;
		profile > MH_AUDIO_PROFILE_SILENT; profile--)
		available[profile] =
			probe(context, (enum mh_audio_profile)profile);

	memset(&catalog, 0, sizeof(catalog));
	catalog.preferred = MH_AUDIO_PROFILE_SILENT;

	for (profile = MH_AUDIO_PROFILE_SILENT;
		profile < MH_AUDIO_PROFILE_COUNT; profile++) {
		if (!available[profile])
			continue;

		catalog.entries[catalog.count++] =
			(enum mh_audio_profile)profile;

		/* Choose the highest available original ID. */
		catalog.preferred = (enum mh_audio_profile)profile;
	}

	*output = catalog;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
