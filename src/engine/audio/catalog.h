#ifndef MH_AUDIO_CATALOG_H
#define MH_AUDIO_CATALOG_H

#include "engine/core/error.h"

/* Original implementation IDs, not native output-device identifiers. */
enum mh_audio_profile {
	MH_AUDIO_PROFILE_SILENT,
	MH_AUDIO_PROFILE_DIRECTSOUND,
	MH_AUDIO_PROFILE_DS3D,
	MH_AUDIO_PROFILE_EAX,
	MH_AUDIO_PROFILE_A3D12,
	MH_AUDIO_PROFILE_A3D20,
	MH_AUDIO_PROFILE_COUNT
};

struct mh_audio_catalog {
	enum mh_audio_profile	entries[MH_AUDIO_PROFILE_COUNT];
	unsigned int		count;
	enum mh_audio_profile	preferred;
};

/*
 * Probe non-silent profiles in original priority order. A nonzero result
 * means available; probing must release temporary resources before returning.
 * The result lists available profiles in menu order, including silence, and
 * chooses the startup default. This neither opens a device nor retries a
 * failed open. Callers must only report processing they actually implement.
 *
 * The callback and its context are borrowed for this call only. No allocation
 * or device ownership is involved. Output is unchanged on invalid arguments.
 */
enum mh_error_code	mh_audio_catalog_collect(
	int (*probe)(void *context, enum mh_audio_profile profile),
	void *context, struct mh_audio_catalog *output,
	struct mh_error *error_out);

#endif
