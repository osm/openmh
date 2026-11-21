#include <stdio.h>
#include <string.h>

#include "backends/render/software_near_clip.h"
#include "engine/core/error.h"

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
	struct mh_near_projection projection;
	struct mh_near_corner corners[4];
	struct mh_software_clipped_face faces[2], saved[2];
	unsigned int i, face_count;

	memset(&projection, 0, sizeof(projection));
	memset(corners, 0, sizeof(corners));
	projection.near_distance = projection.inverse_near = projection.focal =
		projection.aspect = 1;
	projection.half_width = projection.center[0] = projection.center[1] =
		50;
	projection.extent[0] = projection.extent[1] = 100;

	for (i = 0; i < 4; i++) {
		corners[i].camera[0] = i == 1 || i == 2 ? 1 : -1;
		corners[i].camera[1] = i >= 2 ? -1 : 1;
		corners[i].camera[2] = 2;
		corners[i].rgb[0] = (float)(10 + i * 10);
	}

	/* A near-crossing quad retains two independently clipped halves. */
	corners[0].camera[2] = .5f;
	CHECK(mh_software_clip_near(&projection, 11, 1, corners, faces,
		&face_count, NULL) == MH_ERROR_NONE &&
		face_count == 2);
	CHECK(faces[0].count == 4 && faces[1].count == 4);
	CHECK(faces[0].corners[0].weight == 1);
	CHECK(faces[0].corners[1].weight == .5f);
	memcpy(saved, faces, sizeof(saved));

	/*
	 * Software recomputes 1/Z; the hardware cached inverse is not consumed.
	 */
	projection.inverse_near = 123;
	CHECK(mh_software_clip_near(&projection, 11, 1, corners, faces,
		&face_count, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);

	CHECK(mh_software_clip_near(&projection, 11, 15, corners, faces,
		&face_count, NULL) == MH_ERROR_NONE &&
		face_count == 0);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);
	face_count = 77;
	CHECK(mh_software_clip_near(&projection, 11, 16, corners, faces,
		&face_count, NULL) != MH_ERROR_NONE &&
		face_count == 77);
	CHECK(mh_software_clip_near(&projection, 10, 8, corners, faces,
		&face_count, NULL) != MH_ERROR_NONE &&
		face_count == 77);
	projection.near_distance = 0;
	CHECK(mh_software_clip_near(&projection, 11, 1, corners, faces,
		&face_count, NULL) != MH_ERROR_NONE &&
		face_count == 77);
	projection.near_distance = 1;
	corners[1].camera[2] = 0;
	CHECK(mh_software_clip_near(&projection, 11, 1, corners, faces,
		&face_count, NULL) != MH_ERROR_NONE &&
		face_count == 77);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);
	corners[1].camera[2] = 1e-38f;
	CHECK(mh_software_clip_near(&projection, 11, 1, corners, faces,
		&face_count, NULL) != MH_ERROR_NONE &&
		face_count == 77);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);
	corners[1].camera[2] = 2;

	for (i = 0; i < 4; i++)
		corners[i].camera[0] = -100;

	CHECK(mh_software_clip_near(&projection, 11, 1, corners, faces,
		&face_count, NULL) == MH_ERROR_NONE &&
		face_count == 0);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);

	/* Degenerate crossings abort both halves even if one was accepted. */
	for (i = 0; i < 4; i++)
		corners[i].camera[0] = 0;

	corners[0].camera[2] = corners[3].camera[2] = .5f;
	CHECK(mh_software_clip_near(&projection, 11, 8, corners, faces,
		&face_count, NULL) == MH_ERROR_NONE &&
		face_count == 0);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);

	/*
	 * Inconsistent caller masks must not publish unbounded extrapolation.
	 */
	corners[0].camera[2] = 0;
	corners[1].camera[2] = 1e-8f;

	for (i = 0; i < 4; i++)
		corners[i].camera[1] = 0;

	corners[0].uv[0] = -1e9f;
	corners[1].uv[0] = 1e9f;
	face_count = 77;
	CHECK(mh_software_clip_near(&projection, 11, 1, corners, faces,
		&face_count, NULL) != MH_ERROR_NONE &&
		face_count == 77);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);
	puts("Near-plane split, projection, complete culling and invalid-input preservation pass");

	return 0;
}
