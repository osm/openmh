#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/projection.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_projection projection, saved;
	struct mh_projected_point point, previous;
	float transform[12], position[3], camera[3], original[3];
	unsigned int component;

	memset(&point, 0, sizeof(point));
	memset(transform, 0, sizeof(transform));
	transform[0] = transform[4] = transform[8] = 1;
	position[0] = position[1] = 0;
	position[2] = 2;
	CHECK(mh_projection_setup(512, 384, 1.3333333333333335f, 1, .5f, 300,
		&projection, NULL) == MH_ERROR_NONE);
	CHECK(mh_project_point(&projection, transform, position, &point,
		NULL) == MH_ERROR_NONE &&
		point.outcode == 0);
	CHECK(point.value[0] == 256 && point.value[1] == 192 &&
		point.value[3] == .5f);
	CHECK(mh_projection_restore(&projection, &point, camera, NULL) ==
			MH_ERROR_NONE &&
		camera[2] == 2);
	position[2] = .5f;
	previous = point;
	CHECK(mh_project_point(&projection, transform, position, &point,
		NULL) == MH_ERROR_NONE &&
		point.outcode == 16);
	CHECK(point.value[2] == .5f && point.value[3] == previous.value[3]);
	CHECK(mh_projection_restore(&projection, &point, camera, NULL) ==
			MH_ERROR_NONE &&
		memcmp(camera, position, sizeof(camera)) == 0);
	position[2] = 300;
	previous = point;
	CHECK(mh_project_point(&projection, transform, position, &point,
		NULL) == MH_ERROR_NONE &&
		point.outcode == 32);
	CHECK(memcmp(point.value, previous.value, sizeof(point.value)) == 0);
	memcpy(original, camera, sizeof(original));
	CHECK(mh_projection_restore(&projection, &point, camera, NULL) !=
			MH_ERROR_NONE &&
		memcmp(original, camera, sizeof(camera)) == 0);
	saved = projection;
	CHECK(mh_projection_setup(0, 384, 1, 1, .5f, 300, &projection, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&projection, &saved, sizeof(projection)) == 0);
	previous = point;
	transform[0] = 1e10f;
	CHECK(mh_project_point(&projection, transform, position, &point,
		NULL) != MH_ERROR_NONE &&
		memcmp(&point, &previous, sizeof(point)) == 0);
	transform[0] = 1;
	position[0] = 1e9f;
	position[2] = 1;
	CHECK(mh_project_point(&projection, transform, position, &point,
		NULL) != MH_ERROR_NONE &&
		memcmp(&point, &previous, sizeof(point)) == 0);

	/* An output corner may share storage with the input position. */
	for (component = 0; component < 3; component++)
		point.value[component] = component == 2 ? 2 : 0;

	CHECK(mh_project_point(&projection, transform, point.value, &point,
		NULL) == MH_ERROR_NONE &&
		point.outcode == 0);
	CHECK(point.value[0] == 256 && point.value[1] == 192);

	/*
	 * Reject huge reconstruction before any float conversion can overflow.
	 */
	projection.far_distance = 1e9f;
	projection.inverse_focal = projection.inverse_aspect = 1e6f;
	projection.inverse_half_width = 2;
	point.value[0] = point.value[1] = point.value[2] = 1e9f;
	CHECK(mh_projection_restore(&projection, &point, camera, NULL) !=
			MH_ERROR_NONE &&
		memcmp(original, camera, sizeof(camera)) == 0);
	puts("Projection boundaries, reconstruction, aliasing and error preservation pass");

	return 0;
}
