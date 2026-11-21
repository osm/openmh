#include <float.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/software_skid_light.h"

int
main(void)
{
	struct mh_model_lighting lighting;
	struct mh_point_light lights[2];
	float material[3] = {0, 0, 0}, sample[3] = {0, 0, 0};
	float color[3] = {10, 20, 30}, saved[3];
	unsigned int i;

	if (mh_software_skid_color(NULL, NULL, material, color, NULL) !=
			MH_ERROR_NONE ||
		memcmp(color, material, sizeof(color)) != 0)
		return 1;

	material[0] = 80;
	material[1] = 60;
	material[2] = 20;
	memset(&lighting, 0, sizeof(lighting));
	memset(lights, 0, sizeof(lights));
	lighting.lights = lights;
	lighting.light_count = 1;
	lights[0].radius = 10;
	lights[0].intensity = 1;

	for (i = 0; i < 3; i++) {
		lighting.ambient[i] = .2f;
		lighting.brightness[i] = 1;
		lights[0].color[i] = 1;
	}

	if (mh_software_skid_color(&lighting, sample, material, color, NULL) !=
			MH_ERROR_NONE ||
		color[0] != 180 || color[1] != 135 || color[2] != 45)
		return 1;

	lights[0].intensity = -4;

	if (mh_software_skid_color(&lighting, sample, material, color, NULL) !=
			MH_ERROR_NONE ||
		color[0] != -100 || color[1] != -75 || color[2] != -25)
		return 1;

	memcpy(saved, color, sizeof(saved));
	lighting.light_count = 2;
	lights[1].radius = DBL_MAX;

	if (mh_software_skid_color(&lighting, sample, material, color, NULL) !=
			MH_ERROR_INVALID ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	lighting.light_count = 0;
	lighting.ambient[2] = FLT_MAX;

	if (mh_software_skid_color(&lighting, sample, material, color, NULL) !=
			MH_ERROR_RANGE ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	return 0;
}
