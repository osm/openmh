#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/collision_fixture.h"
#include "../support/pair_fixture.h"
#include "engine/assets/col.h"
#include "engine/collision/data.h"
#include "engine/core/binary.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_cells(const struct mh_col *source, const struct mh_collision *collision)
{
	const unsigned char *encoded;
	const unsigned long *indices;
	unsigned long cell[3], count;
	size_t decoded_count, index;

	for (cell[0] = 0; cell[0] < source->dimensions[0]; cell[0]++) {
		for (cell[1] = 0; cell[1] < source->dimensions[1]; cell[1]++) {
			for (cell[2] = 0; cell[2] < source->dimensions[2];
				cell[2]++) {
				encoded = mh_col_cell(source, cell, &count);
				indices = mh_collision_cell(
					collision, cell, &decoded_count);
				CHECK(decoded_count == count);
				CHECK((indices == NULL) == (encoded == NULL));

				for (index = 0;
					index < decoded_count && index < count;
					index++)
					CHECK(indices[index] ==
						mh_read_u16le(
							encoded + index * 2));
			}
		}
	}
}

static void
check_polygons(
	const struct mh_col *source, const struct mh_collision *collision)
{
	struct mh_col_polygon polygon;
	const struct mh_collision_polygon *decoded;
	unsigned long index;

	CHECK(collision->polygon_count == source->polygon_count);

	for (index = 0; index < collision->polygon_count; index++) {
		CHECK(mh_col_polygon(source, index, &polygon));
		decoded = &collision->polygons[index];
		CHECK(decoded->material == polygon.material);
		CHECK(decoded->boundary_count == polygon.boundary_count);
		CHECK(memcmp(decoded->plane, polygon.plane,
			sizeof(polygon.plane)) == 0);
		CHECK(memcmp(decoded->boundaries, polygon.boundaries,
			sizeof(polygon.boundaries)) == 0);
	}
}

static void
test_floor(int tree)
{
	unsigned char bytes[FLOOR_FIXTURE_SIZE];
	struct mh_col source;
	struct mh_collision collision, saved;
	struct mh_error error;
	const unsigned long *indices;
	unsigned long cell[3];
	size_t size, count, length;

	size = collision_floor_fixture(bytes, tree);
	memset(&collision, 0, sizeof(collision));
	CHECK(mh_col_parse(bytes, size, &source, NULL) == MH_ERROR_NONE);
	CHECK(mh_collision_col_decode(bytes, size, &collision, &error) ==
		MH_ERROR_NONE);

	if (collision.cells == NULL)
		return;

	CHECK(error.code == MH_ERROR_NONE && error.message == NULL);
	CHECK(collision.depth == (unsigned int)tree);
	CHECK(collision.root_count == 1 &&
		collision.node_count == (size_t)tree);
	CHECK(collision.leaf_count == 1 && collision.polygon_index_count == 2);
	CHECK(collision.polygons[1].material == 2);
	CHECK(collision.polygons[1].plane[3] == -2);
	CHECK(collision.polygons[0].boundaries[1][0] == -1);
	CHECK(collision.translation[0] == 4 && collision.scale == .125 &&
		collision.cell_size == 8);
	check_cells(&source, &collision);
	check_polygons(&source, &collision);
	saved = collision;

	for (length = 0; length < size; length++) {
		CHECK(mh_collision_col_decode(bytes, length, &collision,
			&error) != MH_ERROR_NONE);
		CHECK(memcmp(&collision, &saved, sizeof(saved)) == 0);
	}

	CHECK(mh_collision_col_decode(bytes, size, NULL, &error) ==
		MH_ERROR_INVALID);

	/*
	 * Parsed bytes are no longer borrowed, including signed boundary
	 * planes.
	 */
	memset(bytes, 0xa5, sizeof(bytes));
	memset(cell, 0, sizeof(cell));
	indices = mh_collision_cell(&collision, cell, &count);
	CHECK(indices != NULL && count == 2);

	if (indices != NULL && count == 2)
		CHECK(indices[0] == 0 && indices[1] == 1);

	CHECK(collision.polygons[1].plane[3] == -2);
	CHECK(collision.polygons[0].boundaries[1][0] == -1);
	cell[0] = collision.dimensions[0];
	CHECK(mh_collision_cell(&collision, cell, &count) == NULL &&
		count == 0);
	mh_collision_free(&collision);
	memset(&saved, 0, sizeof(saved));
	CHECK(memcmp(&collision, &saved, sizeof(saved)) == 0);
	mh_collision_free(&collision);
	mh_collision_free(NULL);
}

static void
test_hull(void)
{
	unsigned char bytes[278];
	struct mh_col source;
	struct mh_collision collision;
	size_t size;

	size = pair_fixture(bytes);
	memset(&collision, 0, sizeof(collision));
	CHECK(mh_col_parse(bytes, size, &source, NULL) == MH_ERROR_NONE);
	CHECK(mh_collision_col_decode(bytes, size, &collision, NULL) ==
		MH_ERROR_NONE);

	if (collision.cells == NULL)
		return;

	CHECK(collision.polygon_count == 6 && collision.leaf_count == 0 &&
		collision.node_count == 0 && collision.polygon_indices == NULL);
	check_cells(&source, &collision);
	check_polygons(&source, &collision);
	mh_collision_free(&collision);
}

/* Two roots, a two-level tree, distinct leaves and repeated polygon IDs. */
static void
test_tree(void)
{
	unsigned char bytes[320], floor[FLOOR_FIXTURE_SIZE];
	struct mh_col source;
	struct mh_collision collision;
	unsigned int child;
	size_t size;

	collision_floor_fixture(floor, 1);
	memset(bytes, 0, sizeof(bytes));
	memcpy(bytes, floor, 40);
	collision_put16(bytes, 44);
	collision_put16(bytes + 2, 2);
	collision_put16(bytes + 4, 8);
	collision_put16(bytes + 6, 4);
	collision_put16(bytes + 8, 4);
	collision_put16(bytes + 12, 3);
	collision_put16(bytes + 14, 5);
	collision_put16(bytes + 40, 1);
	collision_put16(bytes + 42, 0x8003);
	memset(bytes + 44, 0xff, 16);

	for (child = 0; child < 8; child++) {
		collision_put16(bytes + 60 + child * 2,
			child == 6 ? 0 : (child == 3 ? 2 : 0x8001));
		collision_put16(
			bytes + 76 + child * 2, child == 5 ? 0 : 0x8003);
	}

	collision_put16(bytes + 96, 2);
	collision_put16(bytes + 98, 1);
	collision_put16(bytes + 100, 0);
	collision_put16(bytes + 104, 3);
	collision_put16(bytes + 106, 1);
	collision_put16(bytes + 108, 1);
	collision_put16(bytes + 110, 0);
	memcpy(bytes + 112, floor + 86, 120);
	size = 232;
	memset(&collision, 0, sizeof(collision));
	CHECK(mh_col_parse(bytes, size, &source, NULL) == MH_ERROR_NONE);
	CHECK(mh_collision_col_decode(bytes, size, &collision, NULL) ==
		MH_ERROR_NONE);

	if (collision.cells == NULL)
		return;

	CHECK(collision.root_count == 2 && collision.node_count == 2 &&
		collision.leaf_count == 2 &&
		collision.polygon_index_count == 5);
	CHECK(collision.leaves[1].first == 2 && collision.leaves[1].count == 3);
	CHECK(collision.polygon_indices[0] == 1 &&
		collision.polygon_indices[1] == 0 &&
		collision.polygon_indices[2] == 1 &&
		collision.polygon_indices[3] == 1 &&
		collision.polygon_indices[4] == 0);
	check_cells(&source, &collision);
	check_polygons(&source, &collision);
	mh_collision_free(&collision);
}

int
main(void)
{
	test_floor(0);
	test_floor(1);
	test_hull();
	test_tree();

	if (failures != 0)
		return 1;

	puts("Owned COL surfaces, tree/list order, signed planes and byte-independent lifetime passed");

	return 0;
}
