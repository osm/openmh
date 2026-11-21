#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/pose.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	double a[3], r[9], rebuilt[9], saved[9];
	unsigned int i, n;

	memset(r, 0, sizeof(r));
	r[0] = r[4] = r[8] = 1;
	CHECK(mh_pose_angles(r, a, NULL) == MH_ERROR_NONE);
	CHECK(a[0] == 0 && a[1] == 0 && a[2] == 6.283185482025146484375);

	for (n = 0; n < 200; n++) {
		a[0] = n * .031;
		a[1] = n * .074;
		a[2] = n * .011;
		CHECK(mh_pose_matrix(a, r, NULL) == MH_ERROR_NONE);
		CHECK(mh_pose_angles(r, a, NULL) == MH_ERROR_NONE);
		CHECK(mh_pose_matrix(a, rebuilt, NULL) == MH_ERROR_NONE);

		/*
		 * Original vertical branch fixes yaw to zero and uses the same
		 * roll sign at both poles. It is not an inverse at the lower
		 * pole.
		 */
		if (r[6] * r[6] + r[8] * r[8] >= .00001)
			for (i = 0; i < 9; i++)
				CHECK(fabs(r[i] - rebuilt[i]) < .002);
		else
			CHECK(a[1] == 0);
	}

	a[0] = 1.5707963705062866;
	a[1] = a[2] = 0;
	CHECK(mh_pose_matrix(a, r, NULL) == MH_ERROR_NONE &&
		mh_pose_angles(r, a, NULL) == MH_ERROR_NONE);
	CHECK(a[0] == 1.5707963705062866 && a[1] == 0);
	memcpy(saved, r, sizeof(r));
	a[0] = HUGE_VAL;
	CHECK(mh_pose_matrix(a, r, NULL) != MH_ERROR_NONE &&
		memcmp(r, saved, sizeof(r)) == 0);
	a[0] = 77;
	r[0] = 2;
	CHECK(mh_pose_angles(r, a, NULL) != MH_ERROR_NONE && a[0] == 77);
	memset(r, 0, sizeof(r));
	r[0] = -1;
	r[4] = r[8] = 1;
	CHECK(mh_pose_angles(r, a, NULL) != MH_ERROR_NONE && a[0] == 77);
	CHECK(mh_pose_angles(NULL, a, NULL) != MH_ERROR_NONE &&
		mh_pose_angles(r, NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_pose_matrix(NULL, r, NULL) != MH_ERROR_NONE &&
		mh_pose_matrix(a, NULL, NULL) != MH_ERROR_NONE);

	return failures != 0;
}
