#ifndef MH_NAVIGATION_AI_PATH_H
#define MH_NAVIGATION_AI_PATH_H

#include "interfaces/navigation/ai_path.h"

/* Release owned decoded arrays and clear the owner. */
void	mh_ai_path_free(struct mh_ai_path *path);

#endif
