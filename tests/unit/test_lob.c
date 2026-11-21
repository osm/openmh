#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/lob.h"
#include "engine/assets/packed.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "LOB line %d: %s\n", __LINE__, #x);    \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
write_word(unsigned char *data, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		data[i] = (unsigned char)((value >> (8 * i)) & 255);
}

static int
check_invalid_packed(const unsigned char *data, size_t size)
{
	unsigned char sentinel, *output;
	size_t length;

	output = &sentinel;
	length = 123;
	CHECK(mh_packed_decode(data, size, &output, &length, NULL) !=
		MH_ERROR_NONE);
	CHECK(output == &sentinel && length == 123);

	return 0;
}

static int
check_invalid_lob(const unsigned char *data, size_t size)
{
	struct mh_wireframe output, saved;

	memset(&output, 0xa5, sizeof(output));
	saved = output;
	CHECK(mh_wireframe_lob_decode(data, size, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(output)) == 0);

	return 0;
}

static int
packed_tests(void)
{
	unsigned char data[64], *output;
	size_t length, i;
	unsigned int method;

	memset(data, 0, sizeof(data));
	write_word(data, 0x80000004UL);
	data[4] = 254;
	data[5] = 3;
	data[6] = 255;
	data[7] = 17;
	CHECK(mh_packed_decode(data, 8, &output, &length, NULL) ==
			MH_ERROR_NONE &&
		length == 4);
	CHECK(output[0] == 254 && output[1] == 1 && output[2] == 0 &&
		output[3] == 17 && output[4] == 0);
	free(output);

	for (i = 0; i < 8; i++)
		CHECK(check_invalid_packed(data, i) == 0);

	write_word(data, 0);
	CHECK(check_invalid_packed(data, 8) == 0);
	write_word(data, 16UL * 1024 * 1024 + 1);
	CHECK(check_invalid_packed(data, 8) == 0);

	for (method = 0; method < 16; method++) {
		if (method <= 8)
			continue;

		write_word(data, ((unsigned long)method << 27) + 1);
		CHECK(check_invalid_packed(data, 8) == 0);
	}

	/* Escaped marker, maximum run, then literal. */
	write_word(data, 0x08000105UL);
	data[4] = 42;
	data[5] = 42;
	data[6] = 42;
	data[7] = 42;
	data[8] = 99;
	data[9] = 255;
	data[10] = 7;
	CHECK(mh_packed_decode(data, 11, &output, &length, NULL) ==
			MH_ERROR_NONE &&
		length == 261);
	CHECK(output[0] == 42 && output[260] == 7);

	for (i = 1; i < 260; i++)
		CHECK(output[i] == 99);

	free(output);

	for (i = 0; i < 11; i++)
		CHECK(check_invalid_packed(data, i) == 0);

	write_word(data, 0x08000103UL);
	CHECK(check_invalid_packed(data, 11) == 0);

	/* Huffman tree: 0 -> byte 0, 1 -> byte 1. */
	write_word(data, 0x10000004UL);
	write_word(data + 4, 6);
	data[8] = 1;
	data[9] = 3;
	data[10] = 0x60;
	CHECK(mh_packed_decode(data, 11, &output, &length, NULL) ==
			MH_ERROR_NONE &&
		length == 4);
	CHECK(memcmp(output, "\0\1\1\0", 4) == 0);
	free(output);

	for (i = 0; i < 11; i++)
		CHECK(check_invalid_packed(data, i) == 0);

	write_word(data + 4, 0x8000000aUL);
	CHECK(check_invalid_packed(data, 14) == 0); /* width 3 */
	write_word(data + 4, 7);
	CHECK(check_invalid_packed(data, 11) == 0); /* incomplete pair */
	write_word(data + 4, 65);
	CHECK(check_invalid_packed(data, 11) == 0); /* table outside input */
	write_word(data + 4, 4);
	CHECK(check_invalid_packed(data, 11) == 0); /* empty table */
	write_word(data + 4, 6);
	data[8] = 2;
	CHECK(check_invalid_packed(data, 11) == 0); /* invalid child */
	data[8] = 0;
	data[9] = 0;

	/* Cyclic tree, finite input. */
	CHECK(check_invalid_packed(data, 11) == 0);
	write_word(data + 4, 0x3fc00006UL);
	data[8] = 3;
	data[9] = 3;
	CHECK(check_invalid_packed(data, 11) == 0); /* leaf+bias >255 */

	/* Repetition cannot write beyond the advertised output. */
	write_word(data, 0x40000001UL);
	write_word(data + 4, 6);
	data[8] = 1;
	data[9] = 3;
	data[10] = 0x7c;
	CHECK(check_invalid_packed(data, 11) == 0);
	write_word(data, 0x30000001UL);
	CHECK(check_invalid_packed(data, 11) == 0);
	write_word(data, 0x38000001UL);
	CHECK(check_invalid_packed(data, 11) == 0);
	write_word(data, 0x18000001UL);
	data[10] = 0;
	CHECK(check_invalid_packed(data, 11) == 0);
	write_word(data, 0x20000001UL);
	data[10] = 0x20;
	CHECK(check_invalid_packed(data, 11) == 0);
	write_word(data, 0x28000001UL);
	data[10] = 0x10;
	CHECK(check_invalid_packed(data, 11) == 0);

	/*
	 * Adjacent-symbol arithmetic wraps at a byte, including negative
	 * deltas.
	 */
	write_word(data, 0x28000002UL);
	data[10] = 0x60;
	CHECK(mh_packed_decode(data, 11, &output, &length, NULL) ==
			MH_ERROR_NONE &&
		length == 2);
	CHECK(output[0] == 0 && output[1] == 253);
	free(output);
	CHECK(check_invalid_packed(NULL, 4) == 0);

	return 0;
}

int
main(void)
{
	unsigned char data[88], saved[88], packed[92], *wrapped;
	struct mh_wireframe mesh;
	struct mh_wireframe_point points[2], before[2];
	size_t i, size;
	float scale;
	enum mh_error_code status;

	CHECK(packed_tests() == 0);
	memset(data, 0, sizeof(data));
	write_word(data, 2);
	write_word(data + 4, 2);
	write_word(data + 8, 0x40800000UL);
	write_word(data + 40, 0x41400000UL); /* x=4,12 */

	for (i = 0; i < 20; i++) {
		data[20 + i] = (unsigned char)i;
		data[52 + i] = (unsigned char)(i + 32);
	}

	write_word(data + 72, 0);
	write_word(data + 76, 1);
	write_word(data + 80, 1);
	write_word(data + 84, 0);
	memcpy(saved, data, sizeof(data));
	CHECK(mh_wireframe_lob_decode(data, sizeof(data), &mesh, NULL) ==
		MH_ERROR_NONE);
	CHECK(mesh.point_count == 2 && mesh.edge_count == 2 &&
		mesh.edges[1].first == 1);
	CHECK(mh_wireframe_fit(&mesh, 160, &scale, NULL) == MH_ERROR_NONE &&
		scale == 40);
	CHECK(mesh.points[0].xyz[0] == -160 && mesh.points[1].xyz[0] == 160);
	mh_wireframe_free(&mesh);
	CHECK(mesh.points == NULL && mesh.edges == NULL &&
		mesh.point_count == 0);
	CHECK(memcmp(data, saved, sizeof(data)) == 0);

	for (i = 0; i < sizeof(data); i++)
		CHECK(check_invalid_lob(data, i) == 0);

	write_word(data, MH_U32_MAX);
	CHECK(check_invalid_lob(data, sizeof(data)) == 0);
	write_word(data, 0);
	CHECK(check_invalid_lob(data, sizeof(data)) == 0);
	write_word(data, 2);
	write_word(data + 4, MH_U32_MAX);
	CHECK(check_invalid_lob(data, sizeof(data)) == 0);
	write_word(data + 4, 2);
	write_word(data + 76, 2);
	CHECK(check_invalid_lob(data, sizeof(data)) == 0);
	write_word(data + 76, 1);
	write_word(data + 8, 0x7f800000UL);
	CHECK(check_invalid_lob(data, sizeof(data)) == 0);
	write_word(data + 8, 0x7fc00000UL);
	CHECK(check_invalid_lob(data, sizeof(data)) == 0);
	write_word(data + 8, 0x49742410UL);
	CHECK(check_invalid_lob(data, sizeof(data)) == 0);
	CHECK(check_invalid_lob(NULL, sizeof(data)) == 0);
	memcpy(data, saved, sizeof(data));

	/* Both supported wrappers preserve the underlying mesh. */
	CHECK(mh_tbl_encode_named(data, sizeof(data), NULL, &wrapped, &size,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_wireframe_lob_decode(wrapped, size, &mesh, NULL) ==
			MH_ERROR_NONE &&
		mesh.point_count == 2);
	mh_wireframe_free(&mesh);
	free(wrapped);
	write_word(packed, sizeof(data));
	memcpy(packed + 4, data, sizeof(data));
	CHECK(mh_tbl_encode_named(packed, sizeof(packed), NULL, &wrapped, &size,
		NULL) == MH_ERROR_NONE);
	wrapped[3] = 7;
	CHECK(mh_wireframe_lob_decode(wrapped, size, &mesh, NULL) ==
			MH_ERROR_NONE &&
		mesh.point_count == 2);
	mh_wireframe_free(&mesh);
	wrapped[3] = 3;
	CHECK(check_invalid_lob(wrapped, size) == 0);
	free(wrapped);

	/* Failed fitting cannot partially center an imported mesh. */
	memset(&mesh, 0, sizeof(mesh));
	memset(points, 0, sizeof(points));
	mesh.points = points;
	mesh.point_count = 2;
	points[0].xyz[0] = 1;
	points[1].xyz[0] = 1.0e30f;
	memcpy(before, points, sizeof(points));
	scale = 123;
	CHECK(mh_wireframe_fit(&mesh, 160, &scale, NULL) != MH_ERROR_NONE &&
		scale == 123 && memcmp(before, points, sizeof(points)) == 0);
	points[0].xyz[0] = -1.0e-30f;
	points[1].xyz[0] = 1.0e-30f;
	memcpy(before, points, sizeof(points));
	CHECK(mh_wireframe_fit(&mesh, 1000000, &scale, NULL) != MH_ERROR_NONE &&
		scale == 123 && memcmp(before, points, sizeof(points)) == 0);
	CHECK(mh_wireframe_fit(&mesh, -1, &scale, NULL) != MH_ERROR_NONE &&
		scale == 123);
	points[0].xyz[0] = 4;
	points[1].xyz[0] = 12;
	memcpy(before, points, sizeof(points));
	CHECK(mh_wireframe_center_scale(&mesh, 1000000, NULL) !=
			MH_ERROR_NONE &&
		memcmp(before, points, sizeof(points)) == 0);
	CHECK(mh_wireframe_center_scale(&mesh, -1, NULL) != MH_ERROR_NONE &&
		memcmp(before, points, sizeof(points)) == 0);
	CHECK(mh_wireframe_center_scale(&mesh, 3, NULL) == MH_ERROR_NONE);
	CHECK(points[0].xyz[0] == -12 && points[1].xyz[0] == 12);
	points[0].xyz[0] = points[1].xyz[0] = 12;
	status = mh_wireframe_fit(&mesh, 160, &scale, NULL);
	CHECK(status == MH_ERROR_NONE && scale == 0 && points[0].xyz[0] == 0 &&
		points[1].xyz[0] == 0);
	puts("bounded packed resources, LOB parsing and atomic fitting passed");

	return 0;
}
