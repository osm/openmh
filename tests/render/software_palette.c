#include <stdio.h>
#include <string.h>

#include "backends/render/software_palette.h"

static int
invalid_inputs(void)
{
	struct mh_software_triangle triangle;
	struct mh_software_palette palette, saved;
	unsigned char rgb[3];

	memset(&triangle, 0, sizeof(triangle));
	memset(&palette, 0xa5, sizeof(palette));
	memset(rgb, 0, sizeof(rgb));
	memcpy(&saved, &palette, sizeof(saved));

	if (mh_software_palette_build(&palette, rgb, 2, 1, &triangle, 6,
		NULL) != MH_ERROR_INVALID ||
		mh_software_palette_build(&palette, rgb, 3, 0, &triangle, 6,
			NULL) != MH_ERROR_INVALID ||
		mh_software_palette_build(&palette, rgb, 3, 1, &triangle, 8,
			NULL) != MH_ERROR_INVALID)
		return 0;

	triangle.fog = 2;

	if (mh_software_palette_build(
		&palette, rgb, 3, 1, &triangle, 6, NULL) != MH_ERROR_RANGE)
		return 0;

	return memcmp(&palette, &saved, sizeof(saved)) == 0;
}

int
main(void)
{
	return !invalid_inputs();
}
