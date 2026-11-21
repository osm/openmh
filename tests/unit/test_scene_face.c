#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/face_fogged.h"
#include "engine/render/face_vertex_color.h"
#include "engine/render/powervr_environment.h"
#include "engine/render/powervr_face.h"
#include "engine/render/scene_face.h"
#include "engine/render/skid_face.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_direct3d_skids(void)
{
	static const float identity[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
	static const float points[4][3] = {
		{-1, -1, 10}, {1, -1, 10}, {1, 1, 10}, {-1, 1, 10}};

	struct mh_projection projection;
	struct mh_scene_corner corners[4];
	struct mh_scene_face faces[2], saved[2];
	unsigned int i, count;

	CHECK(mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 180, &projection,
		NULL) == MH_ERROR_NONE);
	memset(corners, 0, sizeof(corners));
	memset(faces, 0, sizeof(faces));

	for (i = 0; i < 4; i++)
		CHECK(mh_project_point(&projection, identity, points[i],
			&corners[i].point, NULL) == MH_ERROR_NONE);

	CHECK(mh_skid_face_prepare(&projection, corners, faces, &count, NULL) ==
			MH_ERROR_NONE &&
		count == 1 && faces[0].count == 4);

	for (i = 0; i < 4; i++) {
		CHECK(faces[0].corners[i].rgb[0] == 191 &&
			faces[0].corners[i].rgb[1] == 191 &&
			faces[0].corners[i].rgb[2] == 191);
		CHECK(fabs(faces[0].corners[i].depth - .5 / 9.8) < 1e-7);
		CHECK(faces[0].corners[i].visibility == 255 &&
			faces[0].corners[i].background == 0);
	}

	/* Late invalid geometry must not publish a partial quad or count. */
	memcpy(saved, faces, sizeof(saved));
	corners[3].point.outcode = 128;
	count = 77;
	CHECK(mh_skid_face_prepare(&projection, corners, faces, &count, NULL) !=
			MH_ERROR_NONE &&
		count == 77);
	CHECK(memcmp(saved, faces, sizeof(saved)) == 0);

	return 0;
}

static int
check_powervr_faces(void)
{
	static const float normal[3] = {.25f, -.5f, 0};
	static const float rotation[9] = {0, 1, 0, -1, 0, 0, 0, 0, 1};

	struct mh_projection projection;
	struct mh_scene_corner corners[4];
	struct mh_scene_face faces[2], saved[2];
	unsigned int i, count;
	float uv[2], saved_uv[2];

	CHECK(mh_projection_setup(320, 200, 1.6f, .5f, .5f, 300, &projection,
		NULL) == MH_ERROR_NONE);
	memset(corners, 0, sizeof(corners));
	memset(faces, 0, sizeof(faces));

	for (i = 0; i < 4; i++) {
		corners[i].point.value[0] = i == 1 || i == 2 ? 224 : 96;
		corners[i].point.value[1] = i < 2 ? 60 : 140;
		corners[i].point.value[2] = .7f;
		corners[i].point.value[3] = 1.0f / 210;
		corners[i].uv[0] = .3f;
		corners[i].uv[1] = .7f;
		corners[i].rgb[0] = 40.875f + i;
		corners[i].rgb[1] = 80.875f;
		corners[i].rgb[2] = 120.875f;
	}

	/*
	 * PowerVR preserves texture/RGB at distances Direct3D treats as
	 * ordinary.
	 */
	CHECK(mh_powervr_face_prepare(&projection, MH_SCENE_TEXTURED, 4, 1,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);
	CHECK(faces[0].pass == MH_SCENE_DISTANT_ALPHA);

	for (i = 0; i < 4; i++) {
		CHECK(faces[0].corners[i].rgb[0] == 40 + i);
		CHECK(faces[0].corners[i].uv[0] == .3f &&
			faces[0].corners[i].uv[1] == .7f);
		CHECK(faces[0].corners[i].background == 89 &&
			faces[0].corners[i].visibility == 255);
	}

	CHECK(mh_powervr_face_prepare(&projection, MH_SCENE_SOLID, 4, 0,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);
	CHECK(faces[0].pass == MH_SCENE_DISTANT_SOLID &&
		faces[0].corners[3].rgb[0] == 40);

	/*
	 * One corner at the cutoff keeps the entire face in its material list.
	 */
	corners[0].point.value[2] = .5f;
	CHECK(mh_powervr_face_prepare(&projection, MH_SCENE_TEXTURED, 4, 1,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);
	CHECK(faces[0].pass == MH_SCENE_TEXTURED &&
		faces[0].corners[1].background == 0);
	CHECK(mh_powervr_face_prepare(&projection, MH_SCENE_ENVIRONMENT, 4, 0,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);
	CHECK(faces[0].pass == MH_SCENE_ENVIRONMENT && faces[0].smooth == 1);

	corners[0].point.outcode = MH_CLIP_NEAR;
	corners[0].point.value[0] = -.2f;
	corners[0].point.value[1] = .2f;
	corners[0].point.value[2] = .2f;
	CHECK(mh_powervr_environment_uv(&projection, &corners[0].point, normal,
		uv, NULL) == MH_ERROR_NONE);
	CHECK(fabs(uv[0] - .462575) < 1e-7 && fabs(uv[1] - .47505) < 1e-7);
	CHECK(mh_powervr_environment_model_uv(&projection, &corners[0].point,
		rotation, normal, uv, NULL) == MH_ERROR_NONE);
	CHECK(fabs(uv[0] - .52495) < 1e-7 && fabs(uv[1] - .662175) < 1e-7);
	memcpy(saved_uv, uv, sizeof(saved_uv));
	CHECK(mh_powervr_environment_model_uv(&projection, &corners[0].point,
		NULL, normal, uv, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(saved_uv, uv, sizeof(saved_uv)) == 0);
	CHECK(mh_powervr_face_prepare(&projection, MH_SCENE_ALPHA, 4, 1,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 2);
	CHECK(faces[0].pass == MH_SCENE_ALPHA &&
		faces[0].bucket == faces[1].bucket);

	CHECK(mh_powervr_face_prepare(&projection, MH_SCENE_UNAVAILABLE, 4, 1,
		corners, faces, &count, NULL) == MH_ERROR_NONE);
	CHECK(count == 2 && faces[0].pass == MH_SCENE_UNAVAILABLE &&
		faces[1].pass == MH_SCENE_UNAVAILABLE);
	memcpy(saved, faces, sizeof(saved));
	count = 77;
	corners[3].rgb[0] = 2e9f;
	CHECK(mh_powervr_face_prepare(&projection, MH_SCENE_TEXTURED, 4, 1,
		corners, faces, &count, NULL) == MH_ERROR_INVALID);
	CHECK(count == 77 && memcmp(saved, faces, sizeof(saved)) == 0);

	return 0;
}

static int
check_face_vertex_colors(void)
{
	static const float identity[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
	static const float points[3][3] = {
		{0, .25f, .5f}, {.25f, -.25f, 1.5f}, {-.25f, -.25f, 1.5f}};

	struct mh_projection projection;
	struct mh_scene_corner corners[4];
	struct mh_scene_face faces[2], saved[2];
	unsigned int i, count, intersections;

	CHECK(mh_projection_setup(640, 480, 4.0f / 3, 1, 1, 8, &projection,
		NULL) == MH_ERROR_NONE);
	memset(corners, 0, sizeof(corners));
	memset(faces, 0, sizeof(faces));

	for (i = 0; i < 3; i++) {
		CHECK(mh_project_point(&projection, identity, points[i],
			&corners[i].point, NULL) == MH_ERROR_NONE);
		corners[i].rgb[0] = i == 0 ? .875f : 1.875f;
		corners[i].rgb[1] = 20.875f;
		corners[i].rgb[2] = 40.875f;
	}

	/* Mid-edge red is 1.375 -> 1; rounding the endpoints first gives 0. */
	CHECK(mh_vertex_color_face_prepare(&projection, MH_SCENE_TEXTURED, 3, 1,
		1, corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);
	CHECK(faces[0].count == 4);
	intersections = 0;

	for (i = 0; i < faces[0].count; i++) {
		if (faces[0].corners[i].reciprocal_w == 1) {
			CHECK(faces[0].corners[i].rgb[0] == 1);
			intersections++;
		}

		CHECK(faces[0].corners[i].rgb[1] == 20 &&
			faces[0].corners[i].rgb[2] == 40);
		CHECK(faces[0].corners[i].visibility == 255 &&
			faces[0].corners[i].background == 0);
	}

	CHECK(intersections == 2);

	/*
	 * Flat shading retains source corner zero even when it is clipped out.
	 */
	CHECK(mh_vertex_color_face_prepare(&projection, MH_SCENE_TEXTURED, 3, 0,
		1, corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);

	for (i = 0; i < faces[0].count; i++)
		CHECK(faces[0].corners[i].rgb[0] == 0);

	/* Unclamped emissive blue spills into both other packed channels. */
	corners[0] = corners[1];
	corners[0].rgb[0] = 0;
	corners[0].rgb[1] = 0;
	corners[0].rgb[2] = -1;
	CHECK(mh_vertex_color_face_prepare(&projection, MH_SCENE_SOLID, 3, 0, 1,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);

	for (i = 0; i < faces[0].count; i++)
		CHECK(faces[0].corners[i].rgb[0] == 255 &&
			faces[0].corners[i].rgb[1] == 255 &&
			faces[0].corners[i].rgb[2] == 255);

	for (i = 0; i < 3; i++) {
		corners[i].point.value[2] = .8005602359771729f;
		corners[i].point.value[3] = .15614065f;
	}

	CHECK(mh_vertex_color_face_prepare(&projection, MH_SCENE_ALPHA, 3, 1, 1,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);
	CHECK(faces[0].pass == MH_SCENE_DISTANT_ALPHA &&
		faces[0].corners[0].background == 0);
	memcpy(saved, faces, sizeof(saved));
	count = 77;
	corners[0].rgb[0] = 2e9f;
	CHECK(mh_vertex_color_face_prepare(&projection, MH_SCENE_TEXTURED, 3, 1,
		1, corners, faces, &count, NULL) != MH_ERROR_NONE &&
		count == 77);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);

	return 0;
}

int
main(void)
{
	struct mh_projection projection;
	struct mh_scene_corner corners[4];
	struct mh_scene_face faces[2], saved[2];
	unsigned int i, count;

	CHECK(mh_projection_setup(960, 640, 1.5f, 1, .5f, 300, &projection,
		NULL) == MH_ERROR_NONE);
	memset(corners, 0, sizeof(corners));
	memset(faces, 0, sizeof(faces));

	for (i = 0; i < 4; i++) {
		corners[i].point.value[0] = i == 1 || i == 2 ? 600 : 360;
		corners[i].point.value[1] = i < 2 ? 200 : 440;
		corners[i].point.value[2] = .01f;
		corners[i].point.value[3] = 1.0f / 3;
		corners[i].rgb[0] = 100;
	}

	CHECK(mh_fogged_face_prepare(&projection, MH_SCENE_TEXTURED, 4, 1,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);
	CHECK(faces[0].count == 4 && faces[0].pass == MH_SCENE_TEXTURED &&
		faces[0].corners[0].visibility == 255);
	corners[0].point.outcode = 16;
	corners[0].point.value[0] = -.2f;
	corners[0].point.value[1] = .2f;
	corners[0].point.value[2] = .2f;
	CHECK(mh_fogged_face_prepare(&projection, MH_SCENE_ALPHA, 4, 1, corners,
		faces, &count, NULL) == MH_ERROR_NONE &&
		count == 2);
	CHECK(faces[0].count == 4 && faces[1].count == 4 &&
		faces[0].bucket == faces[1].bucket);
	memcpy(saved, faces, sizeof(saved));
	corners[0].point.outcode = 32;
	CHECK(mh_fogged_face_prepare(&projection, MH_SCENE_TEXTURED, 4, 1,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 0);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);

	for (i = 0; i < 4; i++) {
		corners[i].point.outcode = 0;
		corners[i].point.value[2] = .9f;
		corners[i].point.value[3] = 1.0f / 270;
	}

	CHECK(mh_fogged_face_prepare(&projection, MH_SCENE_ALPHA, 4, 1, corners,
		faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);
	CHECK(faces[0].pass == MH_SCENE_DISTANT_ALPHA &&
		faces[0].corners[0].background == 178);
	CHECK(mh_fogged_face_prepare(&projection, MH_SCENE_UNAVAILABLE, 4, 1,
		corners, faces, &count, NULL) == MH_ERROR_NONE &&
		count == 1);
	CHECK(faces[0].pass == MH_SCENE_UNAVAILABLE &&
		faces[0].corners[0].visibility == 255);
	memcpy(saved, faces, sizeof(saved));
	count = 77;
	corners[0].rgb[0] = -1;
	CHECK(mh_fogged_face_prepare(&projection, MH_SCENE_TEXTURED, 4, 1,
		corners, faces, &count, NULL) != MH_ERROR_NONE &&
		count == 77);
	CHECK(memcmp(faces, saved, sizeof(saved)) == 0);
	CHECK(check_face_vertex_colors() == 0);
	CHECK(check_direct3d_skids() == 0);
	CHECK(check_powervr_faces() == 0);
	puts("Scene quad preservation, near splitting, far rejection, distant classification and atomic errors pass");

	return 0;
}
