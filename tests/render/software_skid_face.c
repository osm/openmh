#include <float.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/software_skid_face.h"

int
main(void)
{
	struct mh_projection projection;
	struct mh_software_skid_face face, saved;
	float camera[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
	float positions[12] = {-1, 1, 2, 1, 1, 2, 1, -1, 2, -1, -1, 2};
	float color[3] = {80, 60, 20};
	unsigned int i;

	if (mh_projection_setup(320, 200, 1.3333333333333335f, 1, 1, 1000,
		&projection, NULL) != MH_ERROR_NONE ||
		mh_software_skid_face_prepare(&projection, camera, positions,
			color, NULL, &face, NULL) != MH_ERROR_NONE ||
		!face.visible || face.count != 2)
		return 1;

	saved = face;
	positions[0] = FLT_MAX;

	if (mh_software_skid_face_prepare(&projection, camera, positions, color,
		NULL, &face, NULL) == MH_ERROR_NONE ||
		memcmp(&face, &saved, sizeof(face)) != 0)
		return 1;

	positions[0] = -1;

	for (i = 0; i < 4; i++)
		positions[i * 3 + 2] = 1;

	if (mh_software_skid_face_prepare(&projection, camera, positions, color,
		NULL, &face, NULL) != MH_ERROR_NONE ||
		face.visible || face.count)
		return 1;

	return 0;
}
