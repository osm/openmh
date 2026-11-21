#include <stdio.h>

#include "engine/render/glenz.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
reflection(void)
{
	float position[3] = {0, 0, 0};
	float normal[3] = {1, 0, 0};
	float eye[3] = {0, 2, -10};
	float uv[2];

	/* Expected coordinates preserve extended-precision intermediates. */
	CHECK(mh_glenz_reflection_uv(position, normal, eye, -99, uv, NULL) ==
		MH_ERROR_NONE);
	CHECK(uv[0] == 0.500019908f && uv[1] == -1.51425457f);

	/* Negative discriminant, then positive discriminant/negative root. */
	position[0] = 6;
	CHECK(mh_glenz_reflection_uv(position, normal, eye, 55, uv, NULL) ==
		MH_ERROR_NONE);
	CHECK(uv[0] == 55 && uv[1] == 55);
	eye[0] = 10;
	eye[1] = 1;
	eye[2] = 0;
	CHECK(mh_glenz_reflection_uv(position, normal, eye, -55, uv, NULL) ==
		MH_ERROR_NONE);
	CHECK(uv[0] == -55 && uv[1] == -55);

	/* Clamp precedes scrolling: the final V can exceed the clamp limit. */
	position[0] = 0;
	position[2] = 40;
	normal[0] = 0;
	normal[2] = 1;
	eye[0] = eye[1] = 0;
	eye[2] = 100000;
	CHECK(mh_glenz_reflection_uv(position, normal, eye, 99, uv, NULL) ==
		MH_ERROR_NONE);
	CHECK(uv[0] == 0.500019908f && uv[1] == 7.96999979f);

	/* Invalid input must not replace the last usable coordinates. */
	CHECK(mh_glenz_reflection_uv(position, normal, position, 0, uv, NULL) ==
		MH_ERROR_INVALID);
	CHECK(uv[0] == 0.500019908f && uv[1] == 7.96999979f);
}

static void
car_color(void)
{
	const unsigned char rgb[3] = {100, 80, 60};
	float brightness[3] = {1, 1, 1};
	float light[3] = {.5f, .5f, .5f};
	float color[3];

	/* Both mode-4 corner paths must produce the same stored colors. */
	CHECK(mh_glenz_car_color(rgb, brightness, light, color, NULL) ==
		MH_ERROR_NONE);
	CHECK(color[0] == 62.5f && color[1] == 52.5f && color[2] == 42.5f);
	light[0] = -2;
	light[1] = 10;
	light[2] = -.5f;
	CHECK(mh_glenz_car_color(rgb, brightness, light, color, NULL) ==
		MH_ERROR_NONE);
	CHECK(color[0] == 0 && color[1] == 255 && color[2] == 0);
	CHECK(mh_glenz_car_color(rgb, NULL, light, color, NULL) ==
		MH_ERROR_INVALID);
	CHECK(color[0] == 0 && color[1] == 255 && color[2] == 0);
}

static void
scrolling(void)
{
	struct mh_glenz_scroll scroll = {{0, 0, 0}, 0};
	float position[3] = {0, 0, 4};
	float forward[3] = {0, 0, 1};

	CHECK(mh_glenz_scroll_update(&scroll, position, forward, NULL) ==
		MH_ERROR_NONE);
	CHECK(scroll.phase == 2 && scroll.previous[2] == 4);
	CHECK(mh_glenz_scroll_update(&scroll, position, forward, NULL) ==
		MH_ERROR_NONE);
	CHECK(scroll.phase == 2);

	position[2] = 0;
	CHECK(mh_glenz_scroll_update(&scroll, position, forward, NULL) ==
		MH_ERROR_NONE);
	CHECK(scroll.phase == 0 && scroll.previous[2] == 0);

	/* A sideways displacement has zero dot product: subtract the step. */
	position[0] = 4;
	CHECK(mh_glenz_scroll_update(&scroll, position, forward, NULL) ==
		MH_ERROR_NONE);
	CHECK(scroll.phase == -2 && scroll.previous[0] == 4);

	scroll.phase = 98;
	position[2] = 4;
	CHECK(mh_glenz_scroll_update(&scroll, position, forward, NULL) ==
		MH_ERROR_NONE);
	CHECK(scroll.phase == 100);
	position[2] = 5;
	CHECK(mh_glenz_scroll_update(&scroll, position, forward, NULL) ==
		MH_ERROR_NONE);
	CHECK(scroll.phase == 1);

	/* A single wrap, including large discontinuities; no fmod shortcut. */
	scroll.phase = 10;
	position[2] += 40000;
	CHECK(mh_glenz_scroll_update(&scroll, position, forward, NULL) ==
		MH_ERROR_NONE);
	CHECK(scroll.phase == 110);
	CHECK(mh_glenz_scroll_update(&scroll, position, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(scroll.phase == 110 && scroll.previous[2] == 40005);
}

static void
phong(void)
{
	float normal[3] = {0, 0, 1};
	float screen[2] = {320, 240};
	float center[2] = {320, 240};
	float uv[2];

	CHECK(mh_glenz_phong_uv(normal, screen, center, 1.0f / 320, uv, NULL) ==
		MH_ERROR_NONE);
	CHECK(uv[0] == .5f && uv[1] == .5f);
	screen[0] = -1000;
	screen[1] = -1000;
	CHECK(mh_glenz_phong_uv(normal, screen, center, 1.0f / 320, uv, NULL) ==
		MH_ERROR_NONE);
	CHECK(uv[0] == 0 && uv[1] == 1);
	CHECK(mh_glenz_phong_uv(normal, screen, center, 0, uv, NULL) ==
		MH_ERROR_INVALID);
	CHECK(uv[0] == 0 && uv[1] == 1);
}

int
main(void)
{
	reflection();
	scrolling();
	phong();
	car_color();

	return failures != 0;
}
