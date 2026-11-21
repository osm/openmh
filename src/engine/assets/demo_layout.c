#include <string.h>

#include "engine/assets/content_probe.h"
#include "engine/assets/demo_layout.h"
#include "engine/assets/path_audio.h"
#include "engine/assets/path_menus.h"

static const struct mh_demo_asset_layout software_first = {
	MH_DEMO_TRACK_SOFTWARE_FIRST,
	3, /* Cars. */
	2, /* Tracks. */
	MH_ASSET_DEMO_SCREEN_FORMAT,
	MH_DEMO_FALLBACK_CPU_ROSTER | MH_DEMO_FALLBACK_SILENT_HORN |
		MH_DEMO_FALLBACK_SMALL_FONT | MH_DEMO_FALLBACK_SUPPORT_ARTWORK,
	MH_ASSET_SOUND_MAJCRASH, /* Alternate crash sample, if needed. */
};

static const struct mh_demo_asset_layout software_second = {
	MH_DEMO_TRACK_SOFTWARE_SECOND,
	3, /* Cars. */
	2, /* Tracks. */
	MH_ASSET_DEMO_SCREEN_FORMAT,
	MH_DEMO_FALLBACK_CPU_ROSTER | MH_DEMO_FALLBACK_SILENT_HORN |
		MH_DEMO_FALLBACK_SMALL_FONT | MH_DEMO_FALLBACK_SUPPORT_ARTWORK,
	NULL, /* Alternate crash sample, if needed. */
};

static const struct mh_demo_asset_layout glide = {
	MH_DEMO_TRACK_GLIDE,
	3, /* Cars. */
	2, /* Tracks. */
	MH_ASSET_DEMO_SCREEN_FORMAT,
	MH_DEMO_FALLBACK_CPU_ROSTER | MH_DEMO_FALLBACK_SILENT_HORN |
		MH_DEMO_FALLBACK_SMALL_FONT | MH_DEMO_FALLBACK_SUPPORT_ARTWORK,
	NULL, /* Alternate crash sample, if needed. */
};

static const struct mh_demo_asset_layout mplayer = {
	MH_DEMO_TRACK_MPLAYER,
	3, /* Cars. */
	1, /* Tracks. */
	MH_ASSET_MPLAYER_SCREEN_FORMAT,
	0,    /* All support resources are supplied. */
	NULL, /* Alternate crash sample, if needed. */
};

struct edition_layout {
	const char				*edition;
	const struct mh_demo_asset_layout	*layout;
};

static const struct edition_layout editions[] = {
	{"motordemo_soft", &software_first},
	{"motordemo_soft_intro", &software_first},
	{"motordemo2_soft", &software_second},
	{"motordemo_3dfx", &glide},
	{"motordemo_3dfx_intro", &glide},
	{"motordemo2_3dfx_intro", &glide},
	{"motordemo_mplayer", &mplayer},
};

enum mh_error_code
mh_demo_asset_layout_probe(const struct mh_resource_source *resources,
	const char *root, const struct mh_demo_asset_layout **output,
	struct mh_error *error_out)
{
	struct mh_content_info content;
	size_t i;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing demo layout output");

	status = mh_content_probe(resources, root, NULL, &content, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < sizeof(editions) / sizeof(editions[0]); i++) {
		if (strcmp(content.edition->id, editions[i].edition) == 0) {
			*output = editions[i].layout;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
		"installation has no original demo resource layout");
}
