#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_fog.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
gradient_guards(void)
{
	struct mh_depth_cue_gradient gradient, saved;
	struct mh_software_point points[4];
	unsigned char pixels[12];
	float color[3];
	unsigned int i;

	memset(&gradient, 0, sizeof(gradient));
	memset(points, 0, sizeof(points));
	memset(pixels, 255, sizeof(pixels));
	gradient.top = 10;
	gradient.above[0] = 1;
	gradient.below[0] = 2;
	CHECK(mh_software_fog_rows(pixels, sizeof(pixels), 2, 3, 6, &gradient,
		NULL) == MH_ERROR_NONE);
	CHECK(gradient.height == 3 && gradient.rows[2][0] == 248 &&
		gradient.rows[2][1] == 252 && gradient.rows[2][2] == 248);
	memset(pixels, 0, sizeof(pixels));
	CHECK(mh_software_fog_rows(pixels, sizeof(pixels), 2, 1, 5, &gradient,
		NULL) == MH_ERROR_NONE);
	CHECK(gradient.height == 1 && gradient.rows[0][0] == 0 &&
		gradient.rows[1][1] == 252 && gradient.top == 10);
	saved = gradient;
	CHECK(mh_software_fog_rows(pixels, 3, 2, 1, 5, &gradient, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_software_fog_rows(pixels, sizeof(pixels), 2, 1, 4, &gradient,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_fog_rows(pixels, sizeof(pixels), 0, 1, 5, &gradient,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_fog_rows(pixels, sizeof(pixels), 2,
		MH_DEPTH_CUE_ROWS + 1, 5, &gradient,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&gradient, &saved, sizeof(gradient)) == 0);

	for (i = 0; i < 4; i++)
		points[i].value[1] = 9;

	CHECK(mh_software_fog_color(&gradient, points, 4, color, NULL) ==
			MH_ERROR_NONE &&
		color[0] == 1);

	for (i = 0; i < 4; i++)
		points[i].value[1] = 10;

	CHECK(mh_software_fog_color(&gradient, points, 4, color, NULL) ==
			MH_ERROR_NONE &&
		color[0] == 0);

	for (i = 0; i < 4; i++)
		points[i].value[1] = 11;

	CHECK(mh_software_fog_color(&gradient, points, 4, color, NULL) ==
			MH_ERROR_NONE &&
		color[1] == 252);

	for (i = 0; i < 4; i++)
		points[i].value[1] = 12;

	CHECK(mh_software_fog_color(&gradient, points, 4, color, NULL) ==
			MH_ERROR_NONE &&
		color[0] == 2);
	points[0].outcode = MH_CLIP_NEAR;
	points[0].value[1] = FLT_MAX;
	CHECK(mh_software_fog_color(&gradient, points, 4, color, NULL) ==
			MH_ERROR_NONE &&
		color[0] == 1);
	points[0].outcode = MH_CLIP_FAR;
	CHECK(mh_software_fog_color(&gradient, points, 4, color, NULL) ==
			MH_ERROR_INVALID &&
		color[0] == 1);
	points[0].outcode = 0;
	CHECK(mh_software_fog_color(&gradient, points, 4, color, NULL) ==
			MH_ERROR_INVALID &&
		color[0] == 1);
	gradient.height = MH_DEPTH_CUE_ROWS;

	for (i = 0; i < 4; i++)
		points[i].value[1] = gradient.top + MH_DEPTH_CUE_ROWS;

	CHECK(mh_software_fog_color(&gradient, points, 4, color, NULL) ==
			MH_ERROR_NONE &&
		color[0] == 2);
	gradient.below[2] = -1;
	CHECK(mh_software_fog_color(&gradient, points, 4, color, NULL) ==
			MH_ERROR_INVALID &&
		color[0] == 2 && color[2] == 0);

	return 0;
}

int
main(void)
{
	struct mh_software_fog fog, saved;
	struct mh_software_point points[4];
	float amount;
	unsigned int i;

	CHECK(gradient_guards() == 0);
	CHECK(mh_software_fog_setup(1, 100, .5f, &fog, NULL) == MH_ERROR_NONE);
	CHECK(fog.start_distance == 50 && fog.inverse_range == .02f);
	saved = fog;
	CHECK(mh_software_fog_setup(0, 100, .5f, &fog, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_software_fog_setup(1, 1, .5f, &fog, NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_fog_setup(1, 100, 1, &fog, NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_fog_setup(1, 100, -1, &fog, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_software_fog_setup(1, FLT_MAX, .5f, &fog, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_software_fog_setup(1, 100, .5f, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_software_fog_setup(1e-9f, 1e-8f, .5f, &fog, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(&fog, &saved, sizeof(fog)) == 0);
	memset(points, 0, sizeof(points));

	for (i = 0; i < 4; i++)
		points[i].value[2] = 75;

	CHECK(mh_software_fog_amount(&fog, points, 4, &amount, NULL) ==
		MH_ERROR_NONE);
	CHECK(amount == .5f);
	points[0].outcode = MH_CLIP_NEAR;
	points[0].value[2] = -100;
	CHECK(mh_software_fog_amount(&fog, points, 4, &amount, NULL) ==
		MH_ERROR_NONE);
	CHECK(amount == .13f);
	points[0].outcode = MH_CLIP_FAR;
	amount = -1;
	CHECK(mh_software_fog_amount(&fog, points, 4, &amount, NULL) ==
		MH_ERROR_INVALID);
	CHECK(amount == -1);
	points[0].outcode = 0;
	CHECK(mh_software_fog_amount(&fog, points, 4, &amount, NULL) ==
		MH_ERROR_INVALID);
	CHECK(amount == -1);
	CHECK(mh_software_fog_amount(&fog, points, 2, &amount, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_software_fog_amount(&fog, NULL, 4, &amount, NULL) ==
		MH_ERROR_INVALID);

	for (i = 0; i < 4; i++)
		points[i].value[2] = 50;

	CHECK(mh_software_fog_amount(&fog, points, 4, &amount, NULL) ==
		MH_ERROR_NONE);
	CHECK(amount == 0);

	return 0;
}
