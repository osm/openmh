#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/shadow.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	float matrix[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0};
	float eye[3] = {0, 1, 0}, contacts[9] = {0, 0, 1, 1, 0, -1, -1, 0, -1};
	struct mh_shadow_query query, saved_query;
	struct mh_shadow_shape shape, saved;
	unsigned int i;

	CHECK(mh_shadow_prepare(NULL, matrix, eye, &query, NULL) ==
			MH_ERROR_NONE &&
		query.visible);
	CHECK(query.probes[0][0] == 0 && query.probes[0][1] == 1);
	CHECK(query.probes[1][0] == .5f && query.probes[2][0] == -.5f);
	CHECK(mh_shadow_project(NULL, matrix, &query, contacts, &shape, NULL) ==
			MH_ERROR_NONE &&
		shape.visible);

	for (i = 0; i < 16; i++)
		CHECK(shape.points[i][1] == .04f);

	CHECK(shape.transmission[0] == 220 && shape.transmission[5] == 100);
	saved = shape;
	saved_query = query;
	CHECK(mh_shadow_prepare(NULL, NULL, eye, &query, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&query, &saved_query, sizeof(query)) == 0);
	query.fade = -1;
	CHECK(mh_shadow_project(NULL, matrix, &query, contacts, &shape, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&shape, &saved, sizeof(shape)) == 0);
	query = saved_query;
	contacts[4] = 10;
	CHECK(mh_shadow_project(NULL, matrix, &query, contacts, &shape, NULL) ==
			MH_ERROR_NONE &&
		!shape.visible);
	memset(contacts, 0, sizeof(contacts));
	CHECK(mh_shadow_project(NULL, matrix, &query, contacts, &shape, NULL) ==
			MH_ERROR_NONE &&
		!shape.visible);
	eye[0] = 181;
	CHECK(mh_shadow_prepare(NULL, matrix, eye, &query, NULL) ==
			MH_ERROR_NONE &&
		!query.visible);
	eye[0] = FLT_MAX;
	CHECK(mh_shadow_prepare(NULL, matrix, eye, &query, NULL) !=
		MH_ERROR_NONE);

	return failures != 0;
}
