#include <stdlib.h>
#include <string.h>

#include "engine/navigation/ai_path.h"

void
mh_ai_path_free(struct mh_ai_path *path)
{
	if (path == NULL)
		return;

	free(path->points);
	free(path->sections);
	memset(path, 0, sizeof(*path));
}
