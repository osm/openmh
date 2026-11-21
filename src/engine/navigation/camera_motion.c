#include <stdlib.h>
#include <string.h>

#include "engine/navigation/camera_motion.h"

void
mh_camera_motion_free(struct mh_camera_motion *motion)
{
	if (motion == NULL)
		return;

	free(motion->keys);
	memset(motion, 0, sizeof(*motion));
}
