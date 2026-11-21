#include <stddef.h>
#include <string.h>

#include "engine/assets/content_edition.h"
#include "engine/assets/path_movies.h"

/* Available opening clips in playback order. */
static const struct mh_content_edition editions[] = {
	{"retail-europe", 0, 0, {MH_ASSET_RETAIL_MOVIES, 3},
		MH_CONTENT_MENU_RETAIL, MH_CONTENT_FEATURES_ALL},
	{"motordemo2_3dfx_intro", 3, 2, {MH_ASSET_DEMO2_MOVIES, 4},
		MH_CONTENT_MENU_COMPACT, 0},
	{"motordemo2_soft", 3, 2, {NULL, 0}, MH_CONTENT_MENU_COMPACT, 0},
	{"motordemo_3dfx", 3, 2, {NULL, 0}, MH_CONTENT_MENU_COMPACT, 0},
	{"motordemo_3dfx_intro", 3, 2, {MH_ASSET_DEMO_MOVIES, 2},
		MH_CONTENT_MENU_COMPACT, 0},
	{"motordemo_mplayer", 3, 1, {NULL, 0}, MH_CONTENT_MENU_COMPACT, 0},
	{"motordemo_soft", 3, 2, {NULL, 0}, MH_CONTENT_MENU_COMPACT, 0},
	{"motordemo_soft_intro", 3, 2, {MH_ASSET_DEMO_MOVIES, 2},
		MH_CONTENT_MENU_COMPACT, 0}};

const struct mh_content_edition *
mh_content_edition_find(const char *id)
{
	size_t i;

	if (id == NULL)
		return NULL;

	for (i = 0; i < sizeof(editions) / sizeof(editions[0]); i++)
		if (strcmp(editions[i].id, id) == 0)
			return &editions[i];

	return NULL;
}
