#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/col.h"
#include "engine/collision/data.h"
#include "engine/core/error.h"
#include "engine/simulation/collision.h"

enum {
	QUERY_LINE_BYTES = 512
};

static void
print_polygons(const struct mh_collision *col)
{
	const struct mh_collision_polygon *polygon;
	unsigned long n;
	unsigned int i, j;

	for (n = 0; n < col->polygon_count; n++) {
		polygon = &col->polygons[n];
		printf("polygon\t%lu\t%lu\t%u", n, polygon->material,
			polygon->boundary_count);

		for (i = 0; i < 4; i++)
			printf("\t%.17g", polygon->plane[i]);

		for (i = 0; i < polygon->boundary_count; i++) {
			for (j = 0; j < 4; j++)
				printf("\t%.17g", polygon->boundaries[i][j]);
		}

		putchar('\n');
	}
}

static void
print_cells(const struct mh_collision *col)
{
	const unsigned long *list;
	unsigned long cell[3];
	size_t count, n;

	for (cell[0] = 0; cell[0] < col->dimensions[0]; cell[0]++) {
		for (cell[1] = 0; cell[1] < col->dimensions[1]; cell[1]++) {
			for (cell[2] = 0; cell[2] < col->dimensions[2];
				cell[2]++) {
				list = mh_collision_cell(col, cell, &count);
				printf("cell\t%lu\t%lu\t%lu", cell[0], cell[1],
					cell[2]);

				for (n = 0; n < count; n++)
					printf("\t%lu", list[n]);

				putchar('\n');
			}
		}
	}
}

static enum mh_error_code
cast_queries(const struct mh_collision *col, struct mh_error *error_out)
{
	struct mh_contact contact;
	enum mh_error_code status;
	unsigned long query;
	unsigned int i;
	int used;
	char line[QUERY_LINE_BYTES];
	double start[3], end[3];

	query = 0;

	while (fgets(line, sizeof(line), stdin) != NULL) {
		used = 0;

		if (sscanf(line, "%lf %lf %lf %lf %lf %lf %n", &start[0],
			&start[1], &start[2], &end[0], &end[1], &end[2],
			&used) != 6 ||
			used == 0 || line[used] != 0) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"expected one bounded line of six query coordinates");
		}

		status =
			mh_collision_cast(col, start, end, &contact, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		printf("query\t%lu\t%d\t%lu\t%lu\t%.17g", query++, contact.hit,
			contact.polygon, contact.material, contact.fraction);

		for (i = 0; i < 3; i++)
			printf("\t%.17g", contact.position[i]);

		for (i = 0; i < 3; i++)
			printf("\t%.17g", contact.normal[i]);

		printf("\t%.17g\t%lu\n", contact.plane_distance,
			(unsigned long)contact.polygon_tests);
	}

	if (ferror(stdin))
		return mh_error_set(
			error_out, MH_ERROR_IO, "failed reading query input");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct mh_col col;
	struct mh_collision collision;
	struct mh_error error;
	enum mh_error_code status;

	if (argc < 2 || argc > 3 ||
		(argc == 3 && strcmp(argv[2], "--queries") != 0 &&
			strcmp(argv[2], "--polygons") != 0 &&
			strcmp(argv[2], "--cells") != 0)) {
		fprintf(stderr,
			"Usage: mh-col-info FILE.col [--polygons | --cells | --queries]\n"
			"Queries read six numbers per line from stdin: start XYZ, end XYZ.\n");
		return 1;
	}

	memset(&collision, 0, sizeof(collision));
	status = mh_file_read(argv[1], &file, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	status = mh_col_parse(file.data, file.size, &col, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	printf("col\t%lu\t%lu\t%lu\t%u\t%lu\t%lu\t%lu\t%lu\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\n",
		col.dimensions[0], col.dimensions[1], col.dimensions[2],
		col.depth, col.node_count, col.list_units, col.polygon_count,
		col.boundary_count, col.translation[0], col.translation[1],
		col.translation[2], col.scale, col.cell_size);

	if (argc == 2)
		goto done;

	status = mh_collision_col_decode(
		file.data, file.size, &collision, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (strcmp(argv[2], "--polygons") == 0)
		print_polygons(&collision);
	else if (strcmp(argv[2], "--cells") == 0)
		print_cells(&collision);
	else
		status = cast_queries(&collision, &error);

done:
	mh_collision_free(&collision);
	free(file.data);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s: %s\n", argv[1], error.message);

	return status == MH_ERROR_NONE && fflush(stdout) == 0 && !ferror(stdout)
		? 0
		: 1;
}
