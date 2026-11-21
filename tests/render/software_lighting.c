#include <float.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/software_lighting.h"

static int
prepare_guards(void)
{
	struct mh_point_light light;
	struct mh_prepared_light result, saved;
	float transform[12], eye[3];

	memset(&light, 0, sizeof(light));
	memset(transform, 0, sizeof(transform));
	memset(eye, 0, sizeof(eye));
	transform[0] = transform[4] = transform[8] = 1;
	light.radius = 100;
	light.intensity = 1;
	light.color[0] = 1;

	if (mh_software_light_prepare(&light, transform, eye, 1, &result,
		NULL) != MH_ERROR_NONE ||
		result.direction[0] != 0 || result.direction[1] != 0 ||
		result.direction[2] != 0 || result.view[0] != 0 ||
		result.view[1] != 0 || result.view[2] != 0 ||
		result.color[0] != (double).4f)
		return 1;

	saved = result;
	light.radius = DBL_MAX;

	if (mh_software_light_prepare(&light, transform, eye, 1, &result,
		NULL) != MH_ERROR_INVALID ||
		memcmp(&result, &saved, sizeof(result)) != 0)
		return 1;

	light.radius = 100;
	light.position[0] = FLT_MAX;

	if (mh_software_light_prepare(&light, transform, eye, 1, &result,
		NULL) != MH_ERROR_RANGE ||
		memcmp(&result, &saved, sizeof(result)) != 0)
		return 1;

	if (mh_software_light_prepare(&light, transform, eye, -1, &result,
		NULL) != MH_ERROR_INVALID ||
		memcmp(&result, &saved, sizeof(result)) != 0)
		return 1;

	return 0;
}

static int
vertex_color_guards(void)
{
	const unsigned char rgb[3] = {255, 128, 64};
	float brightness[3], shade[3], color[3], saved[3];
	unsigned int i;

	for (i = 0; i < 3; i++) {
		brightness[i] = 200;
		shade[i] = 1;
	}

	if (mh_software_light_vertex_color(
		rgb, brightness, shade, 0, color, NULL) != MH_ERROR_NONE ||
		color[0] != 51000 ||
		mh_software_light_vertex_color(rgb, brightness, shade, 1, color,
			NULL) != MH_ERROR_NONE ||
		color[0] != 38250)
		return 1;

	memcpy(saved, color, sizeof(saved));

	if (mh_software_light_vertex_color(rgb, brightness, shade, 2, color,
		NULL) != MH_ERROR_INVALID ||
		memcmp(color, saved, sizeof(color)) != 0)
		return 1;

	shade[2] = -1;

	if (mh_software_light_vertex_color(rgb, brightness, shade, 0, color,
		NULL) != MH_ERROR_INVALID ||
		memcmp(color, saved, sizeof(color)) != 0)
		return 1;

	shade[2] = 1;
	brightness[2] = 201;

	if (mh_software_light_vertex_color(rgb, brightness, shade, 0, color,
		NULL) != MH_ERROR_INVALID ||
		memcmp(color, saved, sizeof(color)) != 0)
		return 1;

	return 0;
}

static int
material_guards(void)
{
	unsigned char rgb[3];
	float brightness[3], shade[3], color[3], saved[3];
	unsigned int i;

	for (i = 0; i < 3; i++) {
		rgb[i] = 255;
		brightness[i] = 200;
		shade[i] = 1;
	}

	if (mh_software_light_color(2, 0, rgb, brightness, shade, color,
		NULL) != MH_ERROR_NONE ||
		color[0] != 25600)
		return 1;

	if (mh_software_light_color(2, 16, rgb, brightness, NULL, color,
		NULL) != MH_ERROR_NONE ||
		color[0] != 51000)
		return 1;

	memcpy(saved, color, sizeof(saved));
	brightness[2] = -1;

	if (mh_software_light_color(0, 0, rgb, brightness, shade, color,
		NULL) != MH_ERROR_INVALID ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	brightness[2] = 1;
	shade[2] = 2;

	if (mh_software_light_color(1, 0, rgb, brightness, shade, color,
		NULL) != MH_ERROR_INVALID ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	if (mh_software_light_color(8, 0, rgb, brightness, shade, color,
		NULL) != MH_ERROR_INVALID ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	return 0;
}

static int
guards(void)
{
	struct mh_software_lighting lighting;
	struct mh_prepared_light light;
	float normal[3], color[3], saved[3];

	memset(&lighting, 0, sizeof(lighting));
	memset(&light, 0, sizeof(light));
	normal[0] = normal[1] = 0;
	normal[2] = 1;
	lighting.ambient[0] = -.5f;
	lighting.ambient[1] = .5f;
	lighting.ambient[2] = 2;
	lighting.direction[2] = 1;
	lighting.directional_color[0] = 1;

	if (mh_software_light_shade(&lighting, normal, MH_SOFTWARE_SHADE_NORMAL,
		color, NULL) != MH_ERROR_NONE ||
		color[0] != .5f || color[1] != .5f || color[2] != 1)
		return 1;

	if (mh_software_light_shade(&lighting, NULL, MH_SOFTWARE_SHADE_UNIFORM,
		color, NULL) != MH_ERROR_NONE ||
		color[0] != 0 || color[1] != .5f || color[2] != 1)
		return 1;

	/* Car mode ignores directional light, then applies facing twice. */
	if (mh_software_light_shade(&lighting, normal, MH_SOFTWARE_SHADE_CAR,
		color, NULL) != MH_ERROR_NONE ||
		color[0] != 0 ||
		!(color[1] > .319999f && color[1] < .320001f) || color[2] != 1)
		return 1;

	lighting.lights = &light;
	lighting.count = 1;
	light.color[0] = 1;
	light.color[1] = -1;

	if (mh_software_light_shade(&lighting, NULL, MH_SOFTWARE_SHADE_UNIFORM,
		color, NULL) != MH_ERROR_NONE ||
		color[0] != 1 || color[1] != 0 || color[2] != 1)
		return 1;

	memcpy(saved, color, sizeof(saved));
	light.color[0] = DBL_MAX;

	if (mh_software_light_shade(&lighting, normal, MH_SOFTWARE_SHADE_NORMAL,
		color, NULL) != MH_ERROR_INVALID ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	light.color[0] = 1;
	lighting.direction[2] = normal[2] = FLT_MAX;

	if (mh_software_light_shade(&lighting, normal, MH_SOFTWARE_SHADE_NORMAL,
		color, NULL) != MH_ERROR_RANGE ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	if (mh_software_light_shade(&lighting, normal,
		(enum mh_software_shade)99, color,
		NULL) != MH_ERROR_INVALID ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	return 0;
}

int
main(void)
{
	return guards() || material_guards() || prepare_guards() ||
		vertex_color_guards();
}
