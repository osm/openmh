#include <stdio.h>
#include <string.h>

#include "engine/audio/catalog.h"

struct probe_state {
	unsigned int	mask, order;
	int		available;
};

static int
probe(void *context, enum mh_audio_profile profile)
{
	struct probe_state *state;

	state = context;
	state->order = state->order * 10 + (unsigned int)profile;

	return state->mask & (1U << profile) ? state->available : 0;
}

static int
examples(void)
{
	struct probe_state state;
	struct mh_audio_catalog catalog;
	struct mh_error error;
	unsigned char saved[sizeof(catalog)];

	state.mask = 0;
	state.available = 1;
	state.order = 0;

	if (mh_audio_catalog_collect(probe, &state, &catalog, &error) !=
			MH_ERROR_NONE ||
		error.message != NULL || state.order != 54321 ||
		catalog.count != 1 ||
		catalog.entries[0] != MH_AUDIO_PROFILE_SILENT ||
		catalog.preferred != MH_AUDIO_PROFILE_SILENT)
		return 0;

	/*
	 * Unavailable higher-priority devices do not prevent the EAX default.
	 */
	state.mask = (1U << MH_AUDIO_PROFILE_DIRECTSOUND) |
		(1U << MH_AUDIO_PROFILE_EAX);
	state.available = -1;
	state.order = 0;

	if (mh_audio_catalog_collect(probe, &state, &catalog, NULL) !=
			MH_ERROR_NONE ||
		state.order != 54321 || catalog.count != 3 ||
		catalog.entries[0] != MH_AUDIO_PROFILE_SILENT ||
		catalog.entries[1] != MH_AUDIO_PROFILE_DIRECTSOUND ||
		catalog.entries[2] != MH_AUDIO_PROFILE_EAX ||
		catalog.preferred != MH_AUDIO_PROFILE_EAX)
		return 0;

	memcpy(saved, &catalog, sizeof(saved));

	if (mh_audio_catalog_collect(NULL, &state, &catalog, &error) !=
			MH_ERROR_INVALID ||
		error.message == NULL ||
		memcmp(saved, &catalog, sizeof(saved)) != 0)
		return 0;

	return mh_audio_catalog_collect(probe, &state, NULL, NULL) ==
		MH_ERROR_INVALID;
}

int
main(void)
{

	if (!examples()) {
		fputs("audio catalog policy failed\n", stderr);
		return 1;
	}

	puts("audio probe order, catalog and startup preference passed");

	return 0;
}
