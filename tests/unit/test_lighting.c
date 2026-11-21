#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/myw.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/geometry/world.h"
#include "engine/render/lighting.h"

#define FUZZ_MULTIPLIER 1664525UL
#define FUZZ_INCREMENT 1013904223UL

enum {
	WORLD_BYTES = 824
};

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)
#define NEAR(a, b) CHECK(fabs((a) - (b)) < 1e-12)

static void
put_u32le(unsigned char *output, unsigned long value)
{
	output[0] = (unsigned char)value;
	output[1] = (unsigned char)(value >> 8);
	output[2] = (unsigned char)(value >> 16);
	output[3] = (unsigned char)(value >> 24);
}

/*
 * No geometry; two cells, two signed lights, three nodes with shared tail,
 * one halo. Runtime scratch/padding intentionally contains invalid floats.
 */
static void
make_world(unsigned char data[WORLD_BYTES])
{
	unsigned int i;
	unsigned char *record;

	memset(data, 0, WORLD_BYTES);
	memcpy(data, "!WYM", 4);
	put_u32le(data + 4, WORLD_BYTES);
	put_u32le(data + 8, 2);
	put_u32le(data + 12, 1);
	put_u32le(data + 0x14, 0x40330000UL);
	put_u32le(data + 0x1c, 0x40330000UL);
	put_u32le(data + 0x20, 0xc1200000UL); /* min X=-10 */
	put_u32le(data + 0x58, 240);

	for (i = 0; i < 12; i++)
		put_u32le(data + 0x64 + i * 12, 288);

	put_u32le(data + 0xbc, 2);
	put_u32le(data + 0xc0, 392);
	put_u32le(data + 0xc8, 3);
	put_u32le(data + 0xcc, 24);
	put_u32le(data + 0xd0, 680);
	put_u32le(data + 0xe0, 1);
	put_u32le(data + 0xe4, 120);
	put_u32le(data + 0xe8, 704);
	put_u32le(data + 240, MH_U32_MAX);
	put_u32le(data + 264, MH_U32_MAX);
	put_u32le(data + 272, 16);
	put_u32le(data + 256, 1); /* First cell's halo span. */

	for (i = 0; i < 2; i++) {
		record = data + 288 + i * 196;
		memset(record, 0xff, 196);
		record[0] = 1;
		memset(record + 4, 0, 12);
		put_u32le(record + 8, 0x41200000UL); /* y=10 */
		memset(record + 0x1c, 0, 12);
		put_u32le(record + 0x1c, 0x3f800000UL); /* red */
		put_u32le(record + 0x28, i == 0 ? 0x3f800000UL : 0xbf800000UL);
		put_u32le(record + 0x2c, 0x41a00000UL); /* radius=20 */
	}

	put_u32le(data + 680, 0);
	put_u32le(data + 684, 8);
	put_u32le(data + 688, 196);
	put_u32le(data + 692, MH_U32_MAX);
	put_u32le(data + 696, 0);
	put_u32le(data + 700, 8);
	memset(data + 704, 0xff, 120);
	data[704] = 2;
	put_u32le(data + 708, 2);
	memset(data + 712, 0, 24);
	put_u32le(data + 724, 0x3f800000UL);
	data[752] = 20;
	data[753] = 40;
	data[754] = 80;
}

static void
check_decoded_lights(void)
{
	static const double positions[] = {-10, 9, 28, -28.5, -29};
	static const size_t heads[] = {
		0, 2, MH_WORLD_NO_LIGHT, 0, MH_WORLD_NO_LIGHT};

	unsigned char data[WORLD_BYTES];
	struct mh_world world;
	struct mh_point_light light;
	double position[3];
	size_t cursor, i;
	enum mh_error_code status;

	make_world(data);
	status = mh_world_myw_decode(data, sizeof(data), &world, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return;

	memset(data, 0, sizeof(data));
	CHECK(world.model_count == 0 && world.models == NULL);
	CHECK(world.cells[0].model == MH_WORLD_NO_MODEL);
	CHECK(world.light_count == 2 && world.link_count == 3);
	CHECK(world.links[0].next == 1 && world.links[2].next == 1);
	CHECK(world.links[1].next == MH_WORLD_NO_LIGHT);
	CHECK(world.cells[0].halo_first == 0 && world.cells[0].halo_count == 1);
	CHECK(world.cells[1].halo_count == 0);
	CHECK(world.halos[0].resource == 2 && world.halos[0].color[2] == 80);
	CHECK(world.halos[0].endpoints[1][0] == 1);
	CHECK(world.has_directional);

	position[1] = position[2] = 0;

	for (i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
		position[0] = positions[i];
		CHECK(mh_world_lights_begin(&world, position, &cursor, NULL) ==
			MH_ERROR_NONE);
		CHECK(cursor == heads[i]);

		if (cursor == MH_WORLD_NO_LIGHT)
			continue;

		CHECK(mh_world_light_next(&world, &cursor, &light));
		CHECK(light.intensity == 1 && light.radius == 20);
		CHECK(mh_world_light_next(&world, &cursor, &light));
		CHECK(light.intensity == -1 && light.radius == 20);
		CHECK(!mh_world_light_next(&world, &cursor, &light));
		CHECK(cursor == MH_WORLD_NO_LIGHT);
	}

	position[0] = DBL_MAX;
	cursor = 2;
	CHECK(mh_world_lights_begin(&world, position, &cursor, NULL) !=
		MH_ERROR_NONE);
	CHECK(cursor == 2);
	mh_world_free(&world);

	/*
	 * Invalid optional directional values do not reject an otherwise valid
	 * world.
	 */
	make_world(data);
	put_u32le(data + 0x48, 0x7fc00000UL);
	status = mh_world_myw_decode(data, sizeof(data), &world, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(!world.has_directional);
		mh_world_free(&world);
	}
}

static void
check_world_lights(void)
{
	unsigned char data[WORLD_BYTES];
	unsigned char saved_data[WORLD_BYTES];
	struct mh_myw world;
	struct mh_myw saved;
	struct mh_point_light light;
	struct mh_point_light saved_light;
	struct mh_geometry_halo halo;
	double position[3];
	unsigned long cursor;
	unsigned long seed;
	unsigned long steps;
	int i;
	struct mh_error error;
	enum mh_error_code status;

	make_world(data);
	memcpy(saved_data, data, sizeof(data));
	status = mh_myw_parse(data, sizeof(data), &world, &error);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return;
	}

	saved = world;
	CHECK(world.cell_size[0] == 19 && world.grid_minimum[0] == -10);
	CHECK(mh_myw_light(&world, 1, &light));
	CHECK(light.intensity == -1 && light.radius == 20);
	saved_light = light;
	CHECK(!mh_myw_light(&world, 2, &light));
	CHECK(memcmp(&light, &saved_light, sizeof(light)) == 0);
	CHECK(mh_myw_halo(&world, 0, &halo));
	CHECK(halo.resource == 2 && halo.endpoints[1][0] == 1 &&
		halo.color[2] == 80);
	CHECK(!mh_myw_halo(&world, 1, &halo));
	position[0] = -10;
	position[1] = position[2] = 0;
	CHECK(mh_myw_lights_begin(&world, position, &cursor, NULL) ==
			MH_ERROR_NONE &&
		cursor == 0);
	CHECK(mh_myw_light_next(&world, &cursor, &light) &&
		light.intensity == 1);
	CHECK(mh_myw_light_next(&world, &cursor, &light) &&
		light.intensity == -1);
	CHECK(!mh_myw_light_next(&world, &cursor, &light) &&
		cursor == MH_MYW_NO_LIGHT);
	position[0] = 9;
	CHECK(mh_myw_lights_begin(&world, position, &cursor, NULL) ==
			MH_ERROR_NONE &&
		cursor == 16);
	position[0] = 28;
	CHECK(mh_myw_lights_begin(&world, position, &cursor, NULL) ==
			MH_ERROR_NONE &&
		cursor == MH_MYW_NO_LIGHT);
	position[0] = -28.5; /* Truncates -0.97 to zero, as in the original. */
	CHECK(mh_myw_lights_begin(&world, position, &cursor, NULL) ==
			MH_ERROR_NONE &&
		cursor == 0);
	position[0] = -29;
	CHECK(mh_myw_lights_begin(&world, position, &cursor, NULL) ==
			MH_ERROR_NONE &&
		cursor == MH_MYW_NO_LIGHT);
	position[0] = DBL_MAX;
	cursor = 16;
	CHECK(mh_myw_lights_begin(&world, position, &cursor, NULL) !=
			MH_ERROR_NONE &&
		cursor == 16);
	CHECK(memcmp(data, saved_data, sizeof(data)) == 0);

	for (i = 0; i < 14; i++) {
		make_world(data);

		if (i == 0)
			put_u32le(data + 684, 0); /* Self cycle. */

		if (i == 1)
			put_u32le(data + 692, 0); /* Two-node cycle. */

		if (i == 2)
			put_u32le(data + 700,
				16); /* Cycle in an unreferenced node. */

		if (i == 2)
			put_u32le(data + 272, MH_U32_MAX);

		if (i == 3)
			put_u32le(data + 680, 1);

		if (i == 4)
			put_u32le(data + 680, 392);

		if (i == 5)
			put_u32le(data + 684, 1);

		if (i == 6)
			put_u32le(data + 684, 24);

		if (i == 7)
			put_u32le(data + 292, 0x7f800000UL);

		if (i == 8)
			put_u32le(data + 0x14, 0x7ff00000UL);

		if (i == 9)
			put_u32le(data + 0x1c, 0);

		if (i == 10)
			data[288] = 3;

		if (i == 11)
			data[704] = 3;

		if (i == 12)
			put_u32le(data + 712, 0x7fc00000UL);

		if (i == 13)
			put_u32le(data + 0x20, 0xff800000UL);

		CHECK(mh_myw_parse(data, sizeof(data), &world, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&world, &saved, sizeof(world)) == 0);
	}

	seed = 173;

	for (i = 0; i < 4000; i++) {
		make_world(data);
		seed = (seed * FUZZ_MULTIPLIER + FUZZ_INCREMENT) & MH_U32_MAX;
		put_u32le(data + seed % (sizeof(data) - 3), seed);
		world = saved;
		status = mh_myw_parse(data, sizeof(data), &world, &error);

		if (status != MH_ERROR_NONE) {
			CHECK(memcmp(&world, &saved, sizeof(world)) == 0);
		} else {
			position[0] = position[1] = position[2] = 0;
			CHECK(mh_myw_lights_begin(&world, position, &cursor,
				NULL) == MH_ERROR_NONE);
			steps = 0;

			while (mh_myw_light_next(&world, &cursor, &light)) {
				steps++;
				CHECK(steps <=
					world.blocks[MH_MYW_LINKS].count);

				if (steps > world.blocks[MH_MYW_LINKS].count)
					break;
			}
		}
	}
}

static void
check_light_equations(void)
{
	struct mh_point_light light;
	struct mh_prepared_light prepared;
	struct mh_prepared_light saved;
	double origin[3] = {0, 0, 0};
	double eye[3] = {0, 10, 0};
	double normal[3] = {0, 1, 0};
	double ambient[3] = {0.1, 0.2, 0.3};
	double color[3];

	memset(&light, 0, sizeof(light));
	light.position[1] = 10;
	light.color[0] = 1;
	light.color[1] = 0.5;
	light.radius = 20;
	light.intensity = 1;
	CHECK(mh_light_prepare(&light, origin, eye, 1, &prepared, NULL) ==
		MH_ERROR_NONE);
	NEAR(prepared.color[0], 0.25);
	CHECK(mh_light_shade(&prepared, 1, normal, ambient, color, NULL) ==
		MH_ERROR_NONE);
	NEAR(color[0], 0.765); /* .14 + .25*(1.5 diffuse + 1 specular) */
	NEAR(color[1], 0.5925);
	NEAR(color[2], 0.42);
	origin[1] = 5;
	CHECK(mh_light_prepare(&light, origin, eye, 0.25, &prepared, NULL) ==
		MH_ERROR_NONE);
	NEAR(prepared.color[0], 0.140625);
	CHECK(mh_light_prepare(&light, origin, eye, 0, &prepared, NULL) ==
		MH_ERROR_NONE);
	CHECK(prepared.color[0] == 0);
	origin[1] = 0;
	light.intensity = 10;
	CHECK(mh_light_prepare(&light, origin, eye, 1, &prepared, NULL) ==
		MH_ERROR_NONE);
	NEAR(prepared.color[0], 0.4);
	light.intensity = -1;
	CHECK(mh_light_prepare(&light, origin, eye, 1, &prepared, NULL) ==
		MH_ERROR_NONE);
	CHECK(!prepared.specular);
	CHECK(mh_light_shade(&prepared, 1, normal, ambient, color, NULL) ==
		MH_ERROR_NONE);
	NEAR(color[0], 0.04625); /* .14 - .0625*1.5 */
	light.intensity = -10;
	CHECK(mh_light_prepare(&light, origin, eye, 1, &prepared, NULL) ==
		MH_ERROR_NONE);
	NEAR(prepared.color[0], -0.625); /* Negative intensity is not capped. */
	CHECK(mh_light_shade(&prepared, 1, normal, ambient, color, NULL) ==
		MH_ERROR_NONE);
	CHECK(color[0] < 0); /* Clamp belongs to material stage. */
	normal[1] = -1;
	CHECK(mh_light_shade(&prepared, 1, normal, ambient, color, NULL) ==
		MH_ERROR_NONE);
	NEAR(color[0], 0.14);
	normal[1] = 1;
	light.intensity = 1;
	CHECK(mh_light_prepare(&light, origin, eye, 1, &prepared, NULL) ==
		MH_ERROR_NONE);
	prepared.view[1] = 0.5;
	CHECK(mh_light_shade(&prepared, 1, normal, ambient, color, NULL) ==
		MH_ERROR_NONE);
	NEAR(color[0], 0.515); /* Strictly greater than 0.5 for highlight. */
	prepared.view[1] = 0.75;
	CHECK(mh_light_shade(&prepared, 1, normal, ambient, color, NULL) ==
		MH_ERROR_NONE);
	NEAR(color[0], 0.5941015625);
	light.radius = 10;
	CHECK(mh_light_prepare(&light, origin, eye, 1, &prepared, NULL) ==
		MH_ERROR_NONE);
	CHECK(prepared.color[0] == 0);
	light.radius = 9;
	CHECK(mh_light_prepare(&light, origin, eye, 1, &prepared, NULL) ==
		MH_ERROR_NONE);
	CHECK(prepared.color[0] == 0);
	light.radius = 1;
	light.position[1] = 0;
	CHECK(mh_light_prepare(&light, origin, origin, 1, &prepared, NULL) ==
		MH_ERROR_NONE);
	CHECK(prepared.color[0] == 0 && prepared.direction[1] == 0 &&
		prepared.view[1] == 0);
	light.radius = 20;
	CHECK(mh_light_prepare(&light, origin, origin, 1, &prepared, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_light_shade(&prepared, 1, normal, ambient, color, NULL) ==
		MH_ERROR_NONE);
	NEAR(color[0], 0.14); /* Coincident light has no arbitrary direction. */
	saved = prepared;
	CHECK(mh_light_prepare(&light, origin, eye, -1, &prepared, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &prepared, sizeof(saved)) == 0);
	light.position[0] = DBL_MAX;
	CHECK(mh_light_prepare(&light, origin, eye, 1, &prepared, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &prepared, sizeof(saved)) == 0);
	CHECK(mh_light_shade(NULL, 1, normal, ambient, color, NULL) !=
		MH_ERROR_NONE);
	NEAR(color[0], 0.14);
	CHECK(mh_light_shade(NULL, 0, normal, ambient, color, NULL) ==
		MH_ERROR_NONE);
	NEAR(color[2], 0.42);
}

static void
check_binary64(void)
{
	unsigned char bytes[8] = {0, 0, 0, 0, 0, 0, 0xf0, 0x3f};
	double value;

	CHECK(mh_read_f64le(bytes, &value) && value == 1);
	bytes[7] = 0xbf;
	CHECK(mh_read_f64le(bytes, &value) && value == -1);
	memset(bytes, 0, sizeof(bytes));
	CHECK(mh_read_f64le(bytes, &value) && value == 0);
	bytes[0] = 1;
	CHECK(mh_read_f64le(bytes, &value) && value == ldexp(1.0, -1074));
	value = 123;
	put_u32le(bytes + 4, 0x7ff00000UL);
	CHECK(!mh_read_f64le(bytes, &value) && value == 123); /* NaN */
	bytes[0] = 0;
	CHECK(!mh_read_f64le(bytes, &value) && value == 123); /* Infinity */
}

int
main(void)
{
	check_binary64();
	check_world_lights();
	check_decoded_lights();
	check_light_equations();

	if (failures != 0)
		return 1;

	puts("Light lists, halo inputs, binary64 and point-light equations passed");

	return 0;
}
