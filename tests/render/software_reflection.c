#include <float.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/software_reflection.h"

static int
guards(void)
{
	struct mh_projection projection;
	struct mh_software_point point;
	float camera[12], normal[3], uv[2], saved[2];

	if (mh_projection_setup(640, 480, 1.3333333f, 1, 1, 1000, &projection,
		NULL) != MH_ERROR_NONE)
		return 1;

	memset(camera, 0, sizeof(camera));
	memset(normal, 0, sizeof(normal));
	memset(&point, 0, sizeof(point));
	camera[0] = camera[4] = camera[8] = 1;
	point.value[0] = 320;
	point.value[1] = 240;

	if (mh_software_reflection_uv(&projection, camera, normal, &point, 256,
		256, uv, NULL) != MH_ERROR_NONE ||
		uv[0] != 128 || uv[1] != 128)
		return 1;

	memcpy(saved, uv, sizeof(saved));
	point.outcode = MH_CLIP_FAR;

	if (mh_software_reflection_uv(&projection, camera, normal, &point, 256,
		256, uv, NULL) != MH_ERROR_INVALID ||
		memcmp(uv, saved, sizeof(uv)) != 0)
		return 1;

	point.outcode = 0;
	camera[0] = normal[0] = FLT_MAX;

	if (mh_software_reflection_uv(&projection, camera, normal, &point, 256,
		256, uv, NULL) != MH_ERROR_RANGE ||
		memcmp(uv, saved, sizeof(uv)) != 0)
		return 1;

	return 0;
}

int
main(void)
{
	return guards();
}
