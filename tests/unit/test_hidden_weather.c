#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/render/atmosphere_sky.h"
#include "engine/render/hidden_weather.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

#define RNG_MULTIPLIER 1103515245UL
#define RNG_INCREMENT 12345UL

static int
check_thunder(void)
{
	static const unsigned long seeds[4] = {
		790079107UL, 444376707UL, 293774979UL, 4243039875UL};

	const struct mh_weather_style *style, *prior;
	unsigned long random;
	unsigned int seed;

	for (seed = 0; seed < 4; seed++) {
		random = seeds[seed];
		style = NULL;
		CHECK(mh_thunder_frame(&random, &style, NULL) == MH_ERROR_NONE);
		CHECK(style != NULL &&
			(style->brightness[0] == 3) ==
				(seed == 1 || seed == 2));
		CHECK(random ==
			((seeds[seed] * RNG_MULTIPLIER + RNG_INCREMENT) &
				MH_U32_MAX));
	}

	prior = style;
	random = 123;
	CHECK(mh_thunder_frame(NULL, &style, NULL) != MH_ERROR_NONE &&
		style == prior);
	CHECK(mh_thunder_frame(&random, NULL, NULL) != MH_ERROR_NONE &&
		random == 123);
#if ULONG_MAX > MH_U32_MAX
	random = (MH_U32_MAX + 1UL);
	CHECK(mh_thunder_frame(&random, &style, NULL) != MH_ERROR_NONE &&
		random == (MH_U32_MAX + 1UL) && style == prior);
#endif

	return 0;
}

static int
check_water(void)
{
	struct mh_water_frame frame, saved;
	float phase, xy[2], result[2];
	unsigned int step;

	memset(&frame, 0, sizeof(frame));
	phase = 0;

	for (step = 0; step < 125; step++) {
		CHECK(mh_water_frame(&phase, 960, 640, &frame, NULL) ==
			MH_ERROR_NONE);
		CHECK(phase > 0);
	}

	CHECK(mh_water_frame(&phase, 960, 640, &frame, NULL) == MH_ERROR_NONE &&
		phase == 0);
	xy[0] = 960;
	xy[1] = 640;
	CHECK(mh_water_point(&frame, xy, result, NULL) == MH_ERROR_NONE);
	CHECK(result[0] == 960 && result[1] == 640);
	xy[0] = 0;
	xy[1] = 0;
	CHECK(mh_water_point(&frame, xy, result, NULL) == MH_ERROR_NONE &&
		result[1] == -160);
	saved = frame;
	CHECK(mh_water_frame(&phase, 960, 0, &frame, NULL) != MH_ERROR_NONE);
	CHECK(phase == 0 && memcmp(&frame, &saved, sizeof(frame)) == 0);
	CHECK(mh_water_frame(&phase, 960, 1e-30f, &frame, NULL) !=
		MH_ERROR_NONE);
	frame.frequency = -1;
	result[0] = 123;
	result[1] = 456;
	CHECK(mh_water_point(&frame, xy, result, NULL) != MH_ERROR_NONE &&
		result[0] == 123 && result[1] == 456);

	/*
	 * The stored value is above the threshold, while the comparison uses
	 * the extended sum.
	 */
	phase = 6.2331852912902832f;
	CHECK(mh_water_frame(&phase, 960, 640, &frame, NULL) == MH_ERROR_NONE &&
		phase == 6.2831854820251465f);
	CHECK(mh_water_point(&frame, xy, result, NULL) == MH_ERROR_NONE);
	CHECK(mh_water_frame(&phase, 960, 640, &frame, NULL) == MH_ERROR_NONE &&
		phase == 0);

	return 0;
}

int
main(void)
{
	struct mh_weather_camera camera;
	struct mh_weather_vertex sky[8], before[8];
	struct mh_mesh mesh;
	struct mh_vertex vertices[18], saved_vertices[18];
	struct mh_draw commands[3], saved_commands[3];
	unsigned int draw, draws;

	if (check_thunder() != 0 || check_water() != 0)
		return 1;

	memset(&camera, 0, sizeof(camera));
	camera.width = 960;
	camera.height = 640;
	camera.focal = 1;
	camera.forward[2] = 1;
	camera.underwater = 1;
	camera.top = 0x7f60a0ffUL;
	camera.bottom = 0x7f00a080UL;
	CHECK(mh_weather_sky(&camera, sky, &draws, NULL) == MH_ERROR_NONE &&
		draws == 2);
	CHECK(sky[2].xy[1] == 384 && sky[6].xy[1] == 640);
	CHECK(sky[0].reciprocal_w == 10 &&
		sky[2].reciprocal_w == 1.1111111640930176f);
	CHECK(sky[0].color == camera.top && sky[7].color == camera.bottom);
	memcpy(before, sky, sizeof(before));
	camera.focal = -1;
	CHECK(mh_weather_sky(&camera, sky, &draws, NULL) == MH_ERROR_NONE &&
		draws == 0);
	camera.focal = 1;
	camera.position[0] = -1250;
	camera.position[2] = -750;
	CHECK(mh_weather_sky(&camera, sky, &draws, NULL) == MH_ERROR_NONE &&
		draws == 2);
	CHECK(sky[0].uv[0] < before[0].uv[0] && sky[0].uv[1] < before[0].uv[1]);
	memcpy(before, sky, sizeof(before));
	camera.height = 0;
	CHECK(mh_weather_sky(&camera, sky, &draws, NULL) != MH_ERROR_NONE &&
		draws == 2);
	CHECK(memcmp(before, sky, sizeof(before)) == 0);

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(commands, 0, sizeof(commands));
	camera.height = 640;
	mesh.vertices = vertices;
	mesh.vertex_count = 18;
	mesh.draws = commands;
	mesh.draw_count = 3;
	mesh.environment_texture = 2;
	mesh.texture_count = 3;

	for (draw = 0; draw < 3; draw++) {
		commands[draw].first = draw * 6;
		commands[draw].count = 6;
		commands[draw].prelit = 1;
	}

	CHECK(mh_sky_mesh_weather(&mesh, &camera, NULL) == MH_ERROR_NONE);
	CHECK(commands[0].texture == 2 && commands[1].texture == 0 &&
		commands[2].count == 0);
	CHECK(vertices[0].projection_w == .1f &&
		vertices[2].projection_w > .89f);
	CHECK(vertices[0].color[2] == 1 && vertices[2].color[0] == 0);
	CHECK(vertices[0].opacity == (float)(camera.top >> 24) / 255 &&
		vertices[2].opacity == (float)(camera.bottom >> 24) / 255);
	memcpy(saved_vertices, vertices, sizeof(vertices));
	memcpy(saved_commands, commands, sizeof(commands));
	camera.width = 0;
	CHECK(mh_sky_mesh_weather(&mesh, &camera, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved_vertices, vertices, sizeof(vertices)) == 0);
	CHECK(memcmp(saved_commands, commands, sizeof(commands)) == 0);
	camera.width = 960;
	camera.focal = -1;
	CHECK(mh_sky_mesh_weather(&mesh, &camera, NULL) == MH_ERROR_NONE &&
		commands[0].count == 0 && commands[1].count == 0);
	CHECK(mh_sky_mesh_black(&mesh, NULL) == MH_ERROR_NONE &&
		commands[0].count == 6 && vertices[0].projection_w == 10000);
	puts("hidden sky and wave boundaries passed");

	return 0;
}
