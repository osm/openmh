#include <stdio.h>
#include <string.h>

#include "engine/assets/camera_motion.h"
#include "engine/core/error.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

/*
 * Optional MOT inputs expose parsed fields to the asset integration check.
 */
int
main(int argc, char *argv[])
{
	static const char valid[] =
		"LWMO\n1\n9\n2\n"
		"1 2 3 -20 178.5 0 1 1 1\n0 11 -1.675991 13.170332 39.350052\n"
		"4 5 6 7 8 9 -1 -2 -3\n30 -6 2 3 4\n";
	static const char *bad[] = {"LWMO\n1\n9\n0\n", "LWMO\n1\n9\n100001\n",
		"LWMO\n1\n9\n1.5\n", "LWMO\n2\n9\n1\n",
		"LWMO\n1\n9\n1\n0 0 0 0 0 0 0 0 0\n0 nan 0 0 0\n",
		"LWMO\n1\n9\n1\n0 0 0 0 0 0 0 0 0\n0 1 0 0 0 extra\n"};

	struct mh_camera_motion motion;
	struct mh_camera_motion trial;
	struct mh_camera_motion saved;
	struct mh_camera_key *key;
	unsigned char buffer[65536];
	FILE *file;
	size_t size;
	size_t i;
	unsigned int j;
	int arg;
	struct mh_error error;
	enum mh_error_code status;

	memset(&motion, 0, sizeof(motion));
	CHECK(mh_camera_mot_parse((const unsigned char *)valid,
		sizeof(valid) - 1, &motion, NULL) == MH_ERROR_NONE);

	if (motion.keys == NULL)
		return 1;

	CHECK(motion.count == 2 && motion.keys[0].selector == 11);
	CHECK(motion.keys[0].focal == (float)-1.675991);
	CHECK(motion.keys[0].far_factor == (float)13.170332);
	CHECK(motion.keys[0].distance_cap == (float)39.350052);
	CHECK(motion.keys[0].anchor[2] == 3 &&
		motion.keys[0].parameter[0] == -20);
	CHECK(motion.keys[1].selector == -6 &&
		motion.keys[1].parameter[5] == -3);
	saved = motion;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK(mh_camera_mot_parse((const unsigned char *)bad[i],
			strlen(bad[i]), &motion, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&saved, &motion, sizeof(motion)) == 0);
	}

	for (i = 0; i < sizeof(valid) - 2; i++) {
		trial = saved;
		status = mh_camera_mot_parse(
			(const unsigned char *)valid, i, &trial, &error);
		CHECK(status != MH_ERROR_NONE);
		CHECK(memcmp(&saved, &trial, sizeof(trial)) == 0);

		if (status == MH_ERROR_NONE)
			mh_camera_motion_free(&trial);
	}

	memcpy(buffer, valid, sizeof(valid) - 1);
	buffer[sizeof(valid) - 1] = 'x';
	CHECK(mh_camera_mot_parse(buffer, sizeof(valid), &motion, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &motion, sizeof(motion)) == 0);
	mh_camera_motion_free(&motion);
	CHECK(motion.keys == NULL && motion.count == 0);
	mh_camera_motion_free(&motion);

	for (arg = 1; arg < argc; arg++) {
		file = fopen(argv[arg], "rb");

		if (file == NULL)
			return 1;

		size = fread(buffer, 1, sizeof(buffer), file);

		if (ferror(file) || size == sizeof(buffer)) {
			fclose(file);
			return 1;
		}

		fclose(file);
		status = mh_camera_mot_parse(buffer, size, &motion, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s: %s\n", argv[arg], error.message);
			return 1;
		}

		for (i = 0; i < motion.count; i++) {
			key = &motion.keys[i];
			printf("%.9g %.9g %.9g %.9g %.9g", (double)key->frame,
				(double)key->selector, (double)key->focal,
				(double)key->far_factor,
				(double)key->distance_cap);

			for (j = 0; j < 3; j++)
				printf(" %.9g", (double)key->anchor[j]);

			for (j = 0; j < 6; j++)
				printf(" %.9g", (double)key->parameter[j]);

			putchar('\n');
		}

		mh_camera_motion_free(&motion);
	}

	return failures != 0;
}
