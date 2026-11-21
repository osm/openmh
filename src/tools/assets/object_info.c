#include <stdio.h>

#include "engine/assets/object_body.h"
#include "engine/core/error.h"

int
main(void)
{
	struct mh_object_body body;
	double minimum[3], maximum[3], polygon_size;
	unsigned int sound_class, i, j;
	int scanned;
	struct mh_error error;
	enum mh_error_code status;

	while ((scanned = scanf("%lf %lf %lf %lf %lf %lf %lf %u", minimum,
			minimum + 1, minimum + 2, maximum, maximum + 1,
			maximum + 2, &polygon_size, &sound_class)) == 8) {
		status = mh_object_body_bounds(minimum, maximum, polygon_size,
			sound_class, &body, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			return 1;
		}

		printf("%.17g %.17g %.17g %.17g %.17g", body.mass,
			body.inertia[0], body.inertia[1], body.inertia[2],
			body.radius);

		for (i = 0; i < 3; i++)
			printf(" %.17g", body.center[i]);

		for (i = 0; i < MH_OBJECT_BODY_POINTS; i++)
			for (j = 0; j < 3; j++)
				printf(" %.17g", body.corners[i][j]);

		printf(" %u %u\n", body.flags, body.sound_class);
	}

	return scanned != EOF || ferror(stdin) || ferror(stdout) ? 1 : 0;
}
