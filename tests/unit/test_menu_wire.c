#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_background.h"
#include "engine/ui/menu_background_draw.h"
#include "engine/ui/menu_wire.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "menu wire line %d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
background_composition(const struct mh_wireframe *mesh)
{
	static struct mh_menu_background_draw draw;
	struct mh_wireframe models[MH_MENU_BACKGROUND_MODELS];
	struct mh_menu_canvas canvas;
	unsigned char pixels[160], saved_pixels[160];

	memset(models, 0, sizeof(models));
	models[0] = *mesh;
	models[0].point_count = 2;
	models[0].edge_count = 1;
	draw.line_count = 1;
	draw.lines[0].x2 = 1;
	draw.lines[0].brightness = 50;
	draw.model_count = 2;
	draw.models[0].position[2] = 282;
	draw.models[0].brightness = 16;
	draw.models[1] = draw.models[0];
	memset(pixels, 0, sizeof(pixels));
	canvas.pixels = pixels;
	canvas.size = sizeof(pixels);
	canvas.begin = 12;
	canvas.end = 120;
	canvas.pitch = 12;
	CHECK(mh_menu_background_render(&draw, models, 9, 9, &canvas, NULL) ==
		MH_ERROR_NONE);
	CHECK(pixels[12] == 50 && pixels[64] == 32 && pixels[65] == 0 &&
		pixels[0] == 0 && pixels[159] == 0);
	memcpy(saved_pixels, pixels, sizeof(pixels));
	draw.models[1].model = 9;
	CHECK(mh_menu_background_render(&draw, models, 9, 9, &canvas, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0);
	draw.models[1].model = 0;
	draw.models[1].position[2] = -6;
	CHECK(mh_menu_background_render(&draw, models, 9, 9, &canvas, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0);
	draw.models[1].position[2] = 282;
	models[0].edge_count = 65536;
	CHECK(mh_menu_background_render(&draw, models, 9, 9, &canvas, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0);
	draw.model_count = 0;
	draw.line_count = 0;
	CHECK(mh_menu_background_render(&draw, NULL, 9, 9, &canvas, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0);

	return 0;
}

int
main(void)
{
	struct mh_wireframe mesh;
	struct mh_wireframe_point vertices[3], saved[3];
	struct mh_wireframe_edge edges[2];
	struct mh_menu_wire_point projected[3], before[3];
	struct mh_menu_line lines[2], saved_lines[2];
	float position[3];
	unsigned long angle[3];

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	vertices[1].xyz[0] = 1;
	vertices[2].xyz[1] = 1;
	memcpy(saved, vertices, sizeof(saved));
	edges[0].first = 0;
	edges[0].second = 1;
	edges[1].first = 1;
	edges[1].second = 2;
	mesh.points = vertices;
	mesh.edges = edges;
	mesh.point_count = 3;
	mesh.edge_count = 2;
	position[0] = position[1] = 0;
	position[2] = 282;
	angle[0] = angle[1] = angle[2] = 0;
	CHECK(mh_menu_wire_project(&mesh, position, angle, 9, 9, projected, 3,
		NULL) == MH_ERROR_NONE);
	CHECK(projected[0].x == 4 && projected[0].y == 4 &&
		projected[1].x == 5 && projected[1].y == 4);
	CHECK(projected[2].x == 4 && projected[2].y > 4.833f &&
		projected[2].y < 4.834f);
	CHECK(memcmp(vertices, saved, sizeof(saved)) == 0);
	CHECK(mh_menu_wire_lines(&mesh, projected, 16, lines, 2, NULL) ==
		MH_ERROR_NONE);
	CHECK(lines[0].x1 == 4 && lines[0].y1 == 4 && lines[0].x2 == 5 &&
		lines[0].y2 == 4);
	CHECK(lines[1].x1 == 5 && lines[1].x2 == 4 && lines[1].y2 == 4 &&
		lines[1].brightness == 16);
	memcpy(before, projected, sizeof(before));
	memcpy(saved_lines, lines, sizeof(lines));
	CHECK(mh_menu_wire_project(&mesh, position, angle, 9, 9, projected, 2,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(before, projected, sizeof(before)) == 0);
	vertices[2].xyz[2] = -288;
	CHECK(mh_menu_wire_project(&mesh, position, angle, 9, 9, projected, 3,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(before, projected, sizeof(before)) == 0);
	vertices[2].xyz[2] = 0;
	vertices[2].xyz[1] = 1.0e30f;
	CHECK(mh_menu_wire_project(&mesh, position, angle, 9, 9, projected, 3,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(before, projected, sizeof(before)) == 0);
	vertices[2].xyz[1] = 1;
	position[0] = 1000000;
	position[2] = -5.999999f;
	CHECK(mh_menu_wire_project(&mesh, position, angle, 9, 9, projected, 3,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(before, projected, sizeof(before)) == 0);
	position[0] = 0;
	position[2] = -294;
	CHECK(mh_menu_wire_project(&mesh, position, angle, 9, 9, projected, 3,
		NULL) == MH_ERROR_NONE);
	CHECK(projected[1].x == 3);
	position[2] = 282;
	angle[0] = angle[1] = angle[2] = MH_U32_MAX;
	CHECK(mh_menu_wire_project(&mesh, position, angle, 9, 9, projected, 3,
		NULL) == MH_ERROR_NONE);
	memcpy(before, projected, sizeof(before));
	angle[0] = angle[1] = angle[2] = 4095;
	CHECK(mh_menu_wire_project(&mesh, position, angle, 9, 9, projected, 3,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(before, projected, sizeof(before)) == 0);
	edges[1].second = 3;
	CHECK(mh_menu_wire_lines(&mesh, projected, 16, lines, 2, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(saved_lines, lines, sizeof(lines)) == 0);
	edges[1].second = 2;
	projected[2].x = 1.0e30f;
	CHECK(mh_menu_wire_lines(&mesh, projected, 16, lines, 2, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(saved_lines, lines, sizeof(lines)) == 0);

	if (background_composition(&mesh) != 0)
		return 1;

	puts("wireframe projection, edge bounds, retained assets and atomic background composition passed");

	return 0;
}
