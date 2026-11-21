#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/fog.h"

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
	struct mh_fog fog, saved;
	double color[3];
	float previous;
	float depths[4];
	int sample, alpha;

	CHECK(mh_fog_visibility(-FLT_MAX) == 255);
	CHECK(mh_fog_visibility(0) == 255 && mh_fog_visibility(.1f) == 255);
	CHECK(mh_fog_visibility(.2f) == 255);
	CHECK(mh_fog_visibility(.45f) == 127);
	CHECK(mh_fog_visibility(.7f) == 0 && mh_fog_visibility(FLT_MAX) == 0);
	previous = 255;

	for (sample = 0; sample <= 10000; sample++) {
		alpha = mh_fog_visibility((float)(sample / 10000.0));
		CHECK(alpha >= 0 && alpha <= previous);
		previous = (float)alpha;
	}

	color[0] = -10;
	color[1] = 142.875;
	color[2] = 300;
	CHECK(mh_fog_configure(color, 180, &fog, NULL) == MH_ERROR_NONE);
	CHECK(fog.color[0] == 0 && fog.color[1] == (float)(142 / 255.0) &&
		fog.color[2] == 1);
	CHECK(fog.inverse_far == (float)(1.0 / 180));
	saved = fog;
	CHECK(mh_fog_configure(color, 0, &fog, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &fog, sizeof(fog)) == 0);
	CHECK(mh_fog_configure(NULL, 180, &fog, NULL) != MH_ERROR_NONE);
	CHECK(mh_fog_configure(color, 180, NULL, NULL) != MH_ERROR_NONE);
	color[0] = DBL_MAX;
	CHECK(mh_fog_configure(color, 180, &fog, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &fog, sizeof(fog)) == 0);
	depths[0] = depths[1] = depths[2] = depths[3] = .9f;
	CHECK(mh_fog_distant(depths, 4) == 1);
	depths[3] = .7f;
	CHECK(mh_fog_distant(depths, 3) == 1 && mh_fog_distant(depths, 4) == 0);
	CHECK(mh_fog_distant(NULL, 4) == -1 && mh_fog_distant(depths, 2) == -1);
	CHECK(mh_fog_alpha_bucket(depths, 4, 16) == -1);
	CHECK(mh_fog_alpha_bucket(depths, 4, 15) == 199);
	CHECK(mh_fog_background(0) == 0 && mh_fog_background(.8f) == 0);
	CHECK(mh_fog_background(.9f) == 178 && mh_fog_background(1) == 255);
	CHECK(mh_fog_background(FLT_MAX) == 255 &&
		mh_fog_background(-FLT_MAX) == 0);

	/* The patched float start crosses the first byte later. */
	CHECK(mh_fog_background(.8005602359771729f) == 1);
	CHECK(mh_fog_background_f32(.8005602359771729f) == 0);
	CHECK(mh_fog_background_f32(.9f) == 178);
	CHECK(mh_fog_background_f32(1) == 255);

	return failures != 0;
}
