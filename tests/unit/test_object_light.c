#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/object_light.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

struct normal_precision_case {
	const char	*name;
	double		normal[3], direction[3], view[3];
	int		specular;
	double		object, car;
};

static void
check_repeated_normals(unsigned int mode)
{
	static const unsigned int normals[6] = {0, 1, 2, 0, 2, 3};
	struct mh_mesh mesh, isolated;
	struct mh_mesh_face faces[4], face;
	struct mh_vertex source[24], output[24], single[3], expected[3];
	struct mh_prepared_light light;
	double ambient[3] = {.2, .3, .4}, brightness[3] = {1.1, .9, 1.3};
	float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	float raw[72], expected_raw[9];
	unsigned int i, j, policy;
	enum mh_error_code status;

	memset(&mesh, 0, sizeof(mesh));
	memset(faces, 0, sizeof(faces));
	memset(source, 0, sizeof(source));
	memset(&light, 0, sizeof(light));
	mesh.vertices = source;
	mesh.vertex_count = 24;
	mesh.faces = faces;
	mesh.face_count = 4;
	light.direction[1] = light.view[1] = 1;
	light.color[0] = .17;
	light.color[1] = -.08;
	light.specular = 1;

	for (i = 0; i < 4; i++) {
		faces[i].first = i * 6;
		faces[i].count = 6;
		faces[i].type = 9;
		faces[i].flags = i == 3 ? 0x10 : i == 1 ? 0 : 0x20;
		faces[i].color[0] = i == 1 ? 93 : 45;
		faces[i].color[1] = 80;
		faces[i].color[2] = 120;
	}

	for (i = 0; i < 24; i++) {
		source[i].normal[1] = (float)normals[i % 6] * .5f - .5f;
		source[i].position[0] = (float)i;
		source[i].uv[0] = (float)i / 12;
	}

	isolated = mesh;
	isolated.vertices = single;
	isolated.vertex_count = 3;
	isolated.faces = &face;
	isolated.face_count = 1;

	/*
	 * Each reference shades one corner alone, without any earlier corner.
	 */
	for (policy = 0; policy < 3; policy++) {
		status = policy == 2
			? mh_mesh_material_light(&mesh, &light, 1, ambient,
				brightness, matrix, mode, 1, output, 24, raw,
				NULL)
			: (policy == 0 ? mh_mesh_object_light
				: mh_mesh_glenz_light)(&mesh, &light, 1,
				ambient, brightness, matrix, mode, output, 24,
				NULL);

		CHECK(status == MH_ERROR_NONE);

		for (i = 0; i < 24; i++) {
			face = faces[i / 6];
			face.first = 0;
			face.count = 3;
			face.type = 8;

			for (j = 0; j < 3; j++)
				single[j] = source[i];

			status = policy == 2
				? mh_mesh_material_light(&isolated, &light, 1,
					ambient, brightness, matrix, mode, 1,
					expected, 3, expected_raw, NULL)
				: (policy == 0 ? mh_mesh_object_light
					: mh_mesh_glenz_light)(&isolated,
					&light, 1, ambient, brightness,
					matrix, mode, expected, 3, NULL);

			CHECK(status == MH_ERROR_NONE);
			CHECK(memcmp(&output[i], &expected[0],
				sizeof(output[i])) == 0);

			if (policy == 2)
				CHECK(memcmp(raw + i * 3, expected_raw,
					3 * sizeof(float)) == 0);
		}
	}
}

static void
check_color_precision(void)
{
	struct mh_mesh mesh;
	struct mh_mesh_face face;
	struct mh_vertex source[3], output[3];
	float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	double ambient[3], brightness[3], normal[3] = {0, 0, 0};
	float light[3], base, expected;
	long double product;
	unsigned int sample, channel, kind;

	memset(&mesh, 0, sizeof(mesh));
	memset(&face, 0, sizeof(face));
	memset(source, 0, sizeof(source));
	mesh.vertices = source;
	mesh.vertex_count = face.count = 3;
	mesh.faces = &face;
	mesh.face_count = 1;

	/* Compare against the original extended-precision operations. */
	for (kind = 0; kind < 3; kind++) {
		face.type = kind == 0 ? 2 : 8;
		face.flags = kind == 2 ? 0x10 : 0;

		for (sample = 0; sample < 512; sample++) {
			for (channel = 0; channel < 3; channel++) {
				face.color[channel] =
					(unsigned char)((sample * 37 +
								channel * 83) %
						256);
				brightness[channel] = ldexp(
					1.0 + (sample % 31) / 32.0,
					(int)((sample + channel * 11) % 157) -
						149);

				if (brightness[channel] > 200)
					brightness[channel] = 200;

				if (sample % 3 == 0)
					brightness[channel] =
						-brightness[channel];

				ambient[channel] = ldexp(
					1.0 + (sample % 17) / 32.0,
					(int)((sample + channel * 23) % 250) -
						149);
			}

			CHECK(mh_light_shade_mode(NULL, 0, normal, ambient,
				MH_LIGHT_CAR, light,
				NULL) == MH_ERROR_NONE);
			CHECK(mh_mesh_object_light(&mesh, NULL, 0, ambient,
				brightness, matrix, MH_LIGHT_CAR, output,
				3, NULL) == MH_ERROR_NONE);

			for (channel = 0; channel < 3; channel++) {
				product = (long double)(float)
						brightness[channel] *
					(kind == 0 ? 200 : face.color[channel]);

				base = (float)product;
				product = (long double)base *
					(kind == 2 ? 1.0f : light[channel]);
				expected = product < 0	? 0
					: product > 255 ? 255
							: (float)product;
				expected /= 255.0f;
				CHECK(memcmp(&expected,
					&output[0].color[channel],
					sizeof(expected)) == 0);
			}
		}
	}
}

static void
check_normal_precision(void)
{
	static const struct normal_precision_case cases[] = {

	/* These two cancellation checks require an extended mantissa. */
#if LDBL_MANT_DIG > 60
		/* (1 + 2^-60) - 1 must retain its tiny positive remainder. */
		{"diffuse cancellation", {1, 8.673617379884035e-19, -1},
			{1, 1, 1}, {0, 0, 0}, 0, 1.3010426069826053e-18,

			1.3010426069826053e-18},
#endif
		/* 1 - (1 + 2^-23)(1 - 2^-23) = 2^-46. */
		{"product precision", {1, 1.00000011920928955078125, 0},
			{1, -0.99999988079071044921875, 0}, {0, 0, 0}, 0,

			2.1316282072803006e-14, 2.1316282072803006e-14},
		{"large cancelling products", {FLT_MAX, FLT_MAX, 1},
			{FLT_MAX, -FLT_MAX, 1}, {0, 0, 0}, 0, 1.5, 1.5},
		/*
		 * The exact cutoff excludes specular; a retained 2^-60 adds it.
		 */
		{"specular cutoff", {1, 0, 0}, {1, 1, 1}, {1, 0, .5}, 1, 1.5,
			1.5},
#if LDBL_MANT_DIG > 60
		{"specular cancellation", {1, 0, 0}, {1, 1, 1},
			{1, -8.673617379884035e-19, .5}, 1, 1.5625, 1.75},
#endif
		{"negative diffuse", {-1, 0, 0}, {1, 0, 0}, {1, 0, 0}, 1, 0, 0},
		/*
		 * 1.5 times the least binary32 subnormal rounds to two units.
		 */
		{"subnormal product", {1.401298464324817e-45, 0, 0}, {1, 0, 0},
			{0, 0, 0}, 0, 2.802596928649634e-45,

			2.802596928649634e-45}};

	struct mh_prepared_light light;
	double ambient[3] = {0, 0, 0};
	float actual[3], expected[3];
	size_t i;
	unsigned int mode;
	enum mh_error_code status;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(&light, 0, sizeof(light));
		memcpy(light.direction, cases[i].direction,
			sizeof(light.direction));
		memcpy(light.view, cases[i].view, sizeof(light.view));
		light.color[0] = 1;
		light.specular = cases[i].specular;

		for (mode = MH_LIGHT_OBJECT; mode <= MH_LIGHT_CAR; mode += 2) {
			expected[0] =
				(float)(mode == MH_LIGHT_CAR ? cases[i].car
							: cases[i].object);
			expected[1] = expected[2] = 0;
			status = mh_light_shade_mode(&light, 1, cases[i].normal,
				ambient, mode, actual, NULL);

			if (status != MH_ERROR_NONE ||
				memcmp(actual, expected, sizeof(actual)) != 0) {
				fprintf(stderr,
					"%s, lighting mode %u: incorrect stored color\n",
					cases[i].name, mode);
				failures++;
			}
		}
	}
}

static void
check_direct3d(void)
{
	static const unsigned int red[5] = {70, 108, 108, 94, 70};
	struct mh_mesh mesh;
	struct mh_mesh_face face;
	struct mh_vertex source[6], output[6], saved[6];
	struct mh_prepared_light light;
	double ambient[3] = {.5, .5, .5}, brightness[3] = {1, 1, 1};
	float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	unsigned int i, k;
	float raw[18];

	memset(&mesh, 0, sizeof(mesh));
	memset(&face, 0, sizeof(face));
	memset(source, 0, sizeof(source));
	memset(&light, 0, sizeof(light));
	mesh.vertices = source;
	mesh.vertex_count = 6;
	mesh.faces = &face;
	mesh.face_count = 1;
	face.count = 6;
	face.color[0] = 100;
	face.color[1] = 80;
	face.color[2] = 60;

	for (i = 0; i < 6; i++) {
		source[i].position[0] = (float)i;
		source[i].normal[1] = i == 0 ? 1 : -1;
		source[i].color[0] = .9f;
	}

	memcpy(saved, source, sizeof(source));

	/* Ambient .5 becomes .7 light; packing truncates each material. */
	for (i = 0; i < 5; i++) {
		face.type = i == 0 ? 1 : i == 1 ? 3 : i == 2 ? 7 : 9;
		face.flags = i >= 3 ? 0x20 : 0;
		CHECK(mh_mesh_material_light(&mesh, NULL, 0, ambient,
			brightness, matrix, MH_LIGHT_CAR, i != 4, output,
			6, raw, NULL) == MH_ERROR_NONE);

		for (k = 0; k < 6; k++) {
			CHECK(output[k].color[0] == red[i] / 255.0f);
			CHECK(output[k].position[0] == source[k].position[0]);
		}
	}

	/* An unlit first color is replicated; signed G spills into R. */
	face.type = 3;
	face.flags = 0x10;
	brightness[0] = 4;
	brightness[1] = -1;
	CHECK(mh_mesh_material_light(&mesh, NULL, 0, ambient, brightness,
		matrix, MH_LIGHT_CAR, 1, output, 6, raw,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < 6; i++) {
		CHECK(raw[i * 3] == 400 && raw[i * 3 + 1] == -80);
		CHECK(output[i].color[0] == 1);
		CHECK(output[i].color[1] == 176 / 255.0f);
		CHECK(output[i].color[2] == 60 / 255.0f);
	}

	/*
	 * Flat faces retain the first normal; smooth corners remain distinct.
	 */
	brightness[0] = brightness[1] = 1;
	face.flags = 0;
	light.direction[1] = 1;
	light.color[0] = .25;

	for (i = 0; i < 2; i++) {
		face.type = i == 0 ? 1 : 9;
		CHECK(mh_mesh_material_light(&mesh, &light, 1, ambient,
			brightness, matrix, MH_LIGHT_OBJECT, 1, output, 6,
			raw, NULL) == MH_ERROR_NONE);
		CHECK((output[0].color[0] == output[1].color[0]) == (i == 0));
	}

	CHECK(memcmp(source, saved, sizeof(source)) == 0);
}

int
main(void)
{
	struct mh_mesh mesh;
	struct mh_mesh_face face;
	struct mh_vertex source[6], output[6], saved[6];
	struct mh_prepared_light light;
	double ambient[3] = {0, 0, 0}, brightness[3] = {1, 1, 1};
	float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	unsigned int i, k;
	float ordinary, body;

	memset(&mesh, 0, sizeof(mesh));
	memset(&face, 0, sizeof(face));
	memset(source, 0, sizeof(source));
	memset(&light, 0, sizeof(light));
	mesh.vertices = source;
	mesh.vertex_count = 6;
	mesh.faces = &face;
	mesh.face_count = 1;
	face.count = 6;
	face.type = 3;
	face.color[0] = 30;
	face.color[1] = 60;
	face.color[2] = 90;

	for (i = 0; i < 6; i++) {
		source[i].position[0] = (float)i;
		source[i].normal[1] = 1;
		source[i].color[0] = source[i].color[1] = source[i].color[2] =
			1;
	}

	memcpy(saved, source, sizeof(source));
	light.direction[1] = light.view[1] = 1;
	light.color[0] = .1;
	light.specular = 1;
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 2, output, 6, NULL) == MH_ERROR_NONE);
	ordinary = output[0].color[0];
	CHECK(fabs(ordinary - 50.0 / 255) < 1e-7);
	CHECK(output[0].color[1] == 0 && output[0].color[2] == 0);
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 4, output, 6, NULL) == MH_ERROR_NONE);
	body = output[0].color[0];
	CHECK(body > ordinary && fabs(body - 110.0 / 255) < 1e-7);

	/* Signed lights subtract, never brighten by a specular term. */
	ambient[0] = ambient[1] = ambient[2] = .5;
	light.color[0] = -.1;
	light.specular = 0;
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 2, output, 6, NULL) == MH_ERROR_NONE);
	CHECK(output[0].color[0] < output[0].color[1]);

	/* A rotated face looking away receives ambient only. */
	matrix[5] = matrix[10] = -1;
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 2, output, 6, NULL) == MH_ERROR_NONE);
	CHECK(output[0].color[0] == output[0].color[1]);
	matrix[5] = matrix[10] = 1;

	/* Emissive texture uses stored RGB and bypasses all point lights. */
	face.flags = 0x10;
	brightness[1] = 2;
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 4, output, 6, NULL) == MH_ERROR_NONE);
	CHECK(fabs(output[0].color[0] - 30.0 / 255) < 1e-7);
	CHECK(fabs(output[0].color[1] - 120.0 / 255) < 1e-7);

	for (i = 0; i < 6; i++)
		for (k = 0; k < 3; k++) {
			CHECK(output[i].position[k] == source[i].position[k]);
			CHECK(output[i].normal[k] == source[i].normal[k]);
		}

	/*
	 * Repeated shading does not accumulate or modify the source material.
	 */
	CHECK(memcmp(source, saved, sizeof(source)) == 0);

	/*
	 * The original brightness setter retains negative object G/B. A
	 * subtractive light can therefore produce a positive final channel.
	 */
	face.flags = 0;
	brightness[1] = -1;
	ambient[1] = -.5;
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 2, output, 6, NULL) == MH_ERROR_NONE);
	CHECK(output[0].color[1] > 0);
	brightness[1] = 2;
	ambient[1] = .5;
	face.flags = 0;
	face.type = 6;
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 2, output, 6, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(source, output, sizeof(source)) == 0);
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 2, source, 6, NULL) != MH_ERROR_NONE);
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 2, output, 5, NULL) != MH_ERROR_NONE);

	/* Glenz wheel faces inherit the root's mode-4 light and color rule. */
	face.type = 9;
	face.flags = 0x20;
	light.color[0] = .1;
	light.specular = 1;

	for (i = 0; i < 3; i++) {
		ambient[i] = 0;
		brightness[i] = 1;
	}

	CHECK(mh_mesh_glenz_light(&mesh, &light, 1, ambient, brightness, matrix,
		MH_LIGHT_OBJECT, output, 6, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 6; i++) {
		CHECK(fabs(output[i].color[0] - 31.625 / 255) < 1e-7);
		CHECK(output[i].color[1] == 0 && output[i].color[2] == 0);
	}

	/* Unmarked, emissive and ordinary textured faces keep their colors. */
	for (i = 0; i < 3; i++) {
		face.type = i == 2 ? 3 : 9;
		face.flags = i == 0 ? 0 : i == 1 ? 0x30 : 0x20;
		CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient,
			brightness, matrix, MH_LIGHT_OBJECT, saved, 6,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_mesh_glenz_light(&mesh, &light, 1, ambient, brightness,
			matrix, MH_LIGHT_OBJECT, output, 6,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(saved, output, sizeof(output)) == 0);
	}

	face.first = 1;
	CHECK(mh_mesh_object_light(&mesh, &light, 1, ambient, brightness,
		matrix, 2, output, 6, NULL) != MH_ERROR_NONE);

	check_direct3d();
	check_repeated_normals(MH_LIGHT_OBJECT);
	check_repeated_normals(MH_LIGHT_CAR);
	check_color_precision();
	check_normal_precision();

	return failures != 0;
}
