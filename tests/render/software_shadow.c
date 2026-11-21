#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/render/software_shadow.h"

/* Input layout and flattened probe output, in float values. */

int
main(void)
{
	float matrix[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0};
	float contacts[9] = {0, 0, 1, 1, 0, -1, -1, 0, -1};
	struct mh_software_shadow_query query, saved_query;
	struct mh_software_shadow_shape shape, saved_shape;
	unsigned int i;

	if (mh_software_shadow_prepare(NULL, matrix, &query, NULL) !=
			MH_ERROR_NONE ||
		query.rays[0][0][1] != -9 || query.rays[0][1][1] != 2 ||
		query.rays[1][0][0] != .5f || query.rays[2][0][0] != -.5f ||
		mh_software_shadow_project(NULL, matrix, contacts, &shape,
			NULL) != MH_ERROR_NONE ||
		!shape.visible)
		return 1;

	for (i = 0; i < MH_SHADOW_POINTS; i++)
		if (shape.points[i][1] != 0)
			return 1;

	saved_query = query;
	saved_shape = shape;
	matrix[0] = FLT_MAX;

	if (mh_software_shadow_prepare(NULL, matrix, &query, NULL) !=
			MH_ERROR_INVALID ||
		memcmp(&query, &saved_query, sizeof(query)) != 0 ||
		mh_software_shadow_project(NULL, matrix, contacts, &shape,
			NULL) != MH_ERROR_INVALID ||
		memcmp(&shape, &saved_shape, sizeof(shape)) != 0)
		return 1;

	matrix[0] = 1;
	contacts[4] = 10;

	if (mh_software_shadow_project(NULL, matrix, contacts, &shape, NULL) !=
			MH_ERROR_NONE ||
		shape.visible)
		return 1;

	memset(contacts, 0, sizeof(contacts));

	if (mh_software_shadow_project(NULL, matrix, contacts, &shape, NULL) !=
			MH_ERROR_NONE ||
		shape.visible)
		return 1;

	return 0;
}
