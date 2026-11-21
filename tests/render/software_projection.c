#include <stdio.h>
#include <string.h>

#include "backends/render/software_projection.h"
#include "engine/core/error.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
face_guards(void)
{
	static const float transform[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
	struct mh_projection projection;
	struct mh_near_corner corners[4];
	struct mh_software_clipped_face faces[2], saved[2];
	struct mh_software_point points[4], saved_points[4];
	unsigned int i, count;

	CHECK(mh_projection_setup(320, 200, 1.3333333333333335f, 1, 1, 1000,
		&projection, NULL) == MH_ERROR_NONE);
	memset(corners, 0, sizeof(corners));
	memset(faces, 0, sizeof(faces));

	for (i = 0; i < 4; i++) {
		corners[i].camera[0] = i == 1 || i == 2 ? 1 : -1;
		corners[i].camera[1] = i >= 2 ? -1 : 1;
		corners[i].camera[2] = 2;
	}

	CHECK(mh_software_project_face(&projection, transform, MH_NEAR_QUAD,
		corners, faces, &count, points, NULL) == MH_ERROR_NONE);
	CHECK(count == 1 && faces[0].count == 4 &&
		faces[0].corners[0].weight == .5f);
	corners[0].camera[2] = .5f;
	CHECK(mh_software_project_face(&projection, transform, MH_NEAR_QUAD,
		corners, faces, &count, points, NULL) == MH_ERROR_NONE);
	CHECK(count == 2 && faces[0].count == 4 && faces[1].count == 4);
	CHECK(points[0].outcode == MH_CLIP_NEAR && points[0].value[2] == .5f);
	CHECK(points[1].outcode == 0 && points[1].value[2] == 2);
	memcpy(saved_points, points, sizeof(points));
	memcpy(saved, faces, sizeof(saved));
	count = 77;
	CHECK(mh_software_project_face(&projection, NULL, MH_NEAR_QUAD, corners,
		faces, &count, points, NULL) == MH_ERROR_INVALID);
	CHECK(count == 77 && memcmp(faces, saved, sizeof(saved)) == 0);
	CHECK(memcmp(points, saved_points, sizeof(points)) == 0);
	corners[1].camera[2] = 1000;
	CHECK(mh_software_project_face(&projection, transform, MH_NEAR_QUAD,
		corners, faces, &count, NULL, NULL) == MH_ERROR_NONE);
	CHECK(count == 0 && memcmp(faces, saved, sizeof(saved)) == 0);

	return 0;
}

int
main(void)
{
	struct mh_projection projection, saved;
	struct mh_software_point point, previous;
	float transform[12], position[3], camera[3], original[3];
	unsigned int i;

	CHECK(face_guards() == 0);
	memset(&point, 0, sizeof(point));
	memset(transform, 0, sizeof(transform));
	transform[0] = transform[4] = transform[8] = 1;
	position[0] = position[1] = 0;
	position[2] = 2;
	CHECK(mh_projection_setup(512, 384, 1.3333333333333335f, 1, .5f, 300,
		&projection, NULL) == MH_ERROR_NONE);
	CHECK(mh_software_project_point(&projection, transform, position,
		&point, NULL) == MH_ERROR_NONE &&
		point.outcode == 0);
	CHECK(point.value[0] == 256 && point.value[1] == 192 &&
		point.value[3] == .5f && point.value[2] == 2);
	CHECK(mh_software_projection_restore(
		&projection, &point, camera, NULL) == MH_ERROR_NONE &&
		camera[2] == 2);
	position[2] = .5f;
	previous = point;
	CHECK(mh_software_project_point(&projection, transform, position,
		&point, NULL) == MH_ERROR_NONE &&
		point.outcode == 16);
	CHECK(point.value[2] == .5f && point.value[3] == previous.value[3]);
	CHECK(mh_software_projection_restore(
		&projection, &point, camera, NULL) == MH_ERROR_NONE &&
		memcmp(camera, position, sizeof(camera)) == 0);
	position[2] = 300;
	previous = point;
	CHECK(mh_software_project_point(&projection, transform, position,
		&point, NULL) == MH_ERROR_NONE &&
		point.outcode == 32);
	CHECK(memcmp(point.value, previous.value, sizeof(point.value)) == 0);
	memcpy(original, camera, sizeof(original));
	CHECK(mh_software_projection_restore(
		&projection, &point, camera, NULL) != MH_ERROR_NONE &&
		memcmp(original, camera, sizeof(camera)) == 0);
	saved = projection;
	CHECK(mh_projection_setup(0, 384, 1, 1, .5f, 300, &projection, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&projection, &saved, sizeof(projection)) == 0);
	previous = point;
	transform[0] = 1e10f;
	CHECK(mh_software_project_point(&projection, transform, position,
		&point, NULL) != MH_ERROR_NONE &&
		memcmp(&point, &previous, sizeof(point)) == 0);
	transform[0] = 1;
	position[0] = 1e9f;
	position[2] = 1;
	CHECK(mh_software_project_point(&projection, transform, position,
		&point, NULL) != MH_ERROR_NONE &&
		memcmp(&point, &previous, sizeof(point)) == 0);

	/* An output corner may share storage with the input position. */
	for (i = 0; i < 3; i++)
		point.value[i] = i == 2 ? 2 : 0;

	CHECK(mh_software_project_point(&projection, transform, point.value,
		&point, NULL) == MH_ERROR_NONE &&
		point.outcode == 0);
	CHECK(point.value[0] == 256 && point.value[1] == 192);

	/*
	 * Reject huge reconstruction before any float conversion can overflow.
	 */
	projection.far_distance = 1e9f;
	projection.inverse_focal = projection.inverse_aspect = 1e6f;
	projection.inverse_half_width = 2;
	point.value[0] = point.value[1] = point.value[2] = 1e9f;
	CHECK(mh_software_projection_restore(
		&projection, &point, camera, NULL) != MH_ERROR_NONE &&
		memcmp(original, camera, sizeof(camera)) == 0);
	puts("Projection boundaries, reconstruction, aliasing and error preservation pass");

	return 0;
}
