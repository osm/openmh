#ifndef MH_ASSETS_DEMO_LAYOUT_H
#define MH_ASSETS_DEMO_LAYOUT_H

#include "engine/core/error.h"

enum mh_demo_track_layout {
	MH_DEMO_TRACK_SOFTWARE_FIRST,
	MH_DEMO_TRACK_SOFTWARE_SECOND,
	MH_DEMO_TRACK_GLIDE,
	MH_DEMO_TRACK_MPLAYER,
	MH_DEMO_TRACK_LAYOUTS
};

/* Independent fallbacks permitted only when the supplied resource is absent. */
enum mh_demo_fallback {
	MH_DEMO_FALLBACK_CPU_ROSTER = 1U << 0,
	MH_DEMO_FALLBACK_SILENT_HORN = 1U << 1,
	MH_DEMO_FALLBACK_SMALL_FONT = 1U << 2,
	MH_DEMO_FALLBACK_SUPPORT_ARTWORK = 1U << 3
};

struct mh_resource_source;

/*
 * Original resource layout, independent of simulation and renderer selection.
 */
struct mh_demo_asset_layout {
	enum mh_demo_track_layout	track;
	unsigned int			cars, tracks;
	const char			*catalog_pattern;
	unsigned int			fallbacks;
	const char			*crash_sample;
};

/* Hash-verified content probe; no installation writes or global cache. */
enum mh_error_code	mh_demo_asset_layout_probe(
	const struct mh_resource_source *resources, const char *root,
	const struct mh_demo_asset_layout **output, struct mh_error *error_out);

#endif
