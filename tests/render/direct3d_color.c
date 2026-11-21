#include <stdio.h>
#include <string.h>

#include "engine/render/material_color.h"

struct color_case {
	unsigned int		mode;
	enum mh_material_kind	material;
	float			color[3];
};

static int
check_unlit(void)
{
	const unsigned char rgb[3] = {200, 80, 0};
	float brightness[3] = {2, -1, 1}, color[3], saved[3];

	if (mh_prelit_color(rgb, brightness, color, NULL) != MH_ERROR_NONE ||
		color[0] != 255 || color[1] != 0 || color[2] != 0)
		return 1;

	if (mh_emissive_color(rgb, brightness, color, NULL) != MH_ERROR_NONE ||
		color[0] != 400 || color[1] != -80 || color[2] != 0)
		return 1;

	memcpy(saved, color, sizeof(saved));
	brightness[2] = 1e7f;

	if (mh_emissive_color(rgb, brightness, color, NULL) !=
			MH_ERROR_INVALID ||
		memcmp(color, saved, sizeof(saved)) != 0)
		return 1;

	if (mh_prelit_color(rgb, brightness, color, NULL) != MH_ERROR_INVALID ||
		memcmp(color, saved, sizeof(saved)) != 0)
		return 1;

	return 0;
}

static int
check_colors(void)
{
	static const struct color_case cases[] = {
		{MH_LIGHT_OBJECT, MH_MATERIAL_SOLID, {50, 40, 30}},
		{MH_LIGHT_OBJECT, MH_MATERIAL_TEXTURED, {100, 100, 100}},
		{MH_LIGHT_OBJECT, MH_MATERIAL_ENVIRONMENT, {50, 40, 30}},
		{MH_LIGHT_CAR, MH_MATERIAL_SOLID, {50.25f, 40.25f, 30.25f}},
		{MH_LIGHT_CAR, MH_MATERIAL_TEXTURED, {72.5f, 72.5f, 72.5f}},
		{MH_LIGHT_CAR, MH_MATERIAL_ENVIRONMENT, {62.5f, 52.5f, 42.5f}}};

	const unsigned char rgb[3] = {100, 80, 60};
	const float brightness[3] = {1, 1, 1}, light[3] = {.5f, .5f, .5f};
	float color[3], saved[3];
	unsigned int i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (mh_material_color(cases[i].material, cases[i].mode, rgb,
			brightness, light, color, NULL) != MH_ERROR_NONE ||
			memcmp(color, cases[i].color, sizeof(color)) != 0)
			return 1;
	}

	memcpy(saved, color, sizeof(saved));

	if (mh_material_color(MH_MATERIAL_SOLID, MH_LIGHT_UNAVAILABLE, rgb,
		brightness, light, color, NULL) != MH_ERROR_INVALID ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	if (mh_material_color((enum mh_material_kind)3, MH_LIGHT_CAR, rgb,
		brightness, light, color, NULL) != MH_ERROR_INVALID ||
		memcmp(saved, color, sizeof(saved)) != 0)
		return 1;

	return 0;
}

int
main(void)
{
	return check_colors() || check_unlit();
}
