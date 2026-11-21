#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/myw.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track.h"
#include "engine/core/error.h"

#define QUERY_COORDINATE_LIMIT 1e6

static enum mh_error_code
sample_lighting(const struct mh_myw *world, const struct mh_track *track,
	struct mh_prepared_light *prepared, const double position[3],
	const double eye[3], const double *normal, unsigned long index,
	struct mh_error *error_out)
{
	static const double normals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
		{0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

	struct mh_point_light light;
	enum mh_error_code status;
	unsigned long cursor;
	size_t count;
	unsigned int i;
	double color[3];

	status = mh_myw_lights_begin(world, position, &cursor, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	count = 0;

	while (mh_myw_light_next(world, &cursor, &light)) {
		status = mh_light_prepare(&light, position, eye,
			track->light_intensity, &prepared[count++], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < (normal == NULL ? 6U : 1U); i++) {
		status = mh_light_shade(prepared, count,
			normal == NULL ? normals[i] : normal,
			track->object_ambient, color, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		printf("sample\t%lu\t%u\t%lu\t%.17g\t%.17g\t%.17g\n", index, i,
			(unsigned long)count, color[0], color[1], color[2]);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
print_world_lights(const struct mh_myw *world)
{
	struct mh_point_light light;
	struct mh_geometry_halo halo;
	unsigned long n;
	unsigned int i, j;

	printf("world\t%lu\t%lu\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\n",
		world->width, world->height, world->cell_size[0],
		world->cell_size[1], world->grid_minimum[0],
		world->grid_minimum[1], world->grid_minimum[2]);

	for (n = 0; n < world->blocks[MH_MYW_LIGHTS].count; n++) {
		mh_myw_light(world, n, &light);
		printf("light\t%lu", n);

		for (i = 0; i < 3; i++)
			printf("\t%.17g", light.position[i]);

		for (i = 0; i < 3; i++)
			printf("\t%.17g", light.color[i]);

		printf("\t%.17g\t%.17g\n", light.intensity, light.radius);
	}

	for (n = 0; n < world->blocks[MH_MYW_AUXILIARY].count; n++) {
		mh_myw_halo(world, n, &halo);
		printf("halo\t%lu\t%lu", n, halo.resource);

		for (i = 0; i < 2; i++) {
			for (j = 0; j < 3; j++)
				printf("\t%.17g", halo.endpoints[i][j]);
		}

		for (i = 0; i < 3; i++)
			printf("\t%u", (unsigned int)halo.color[i]);

		putchar('\n');
	}
}

static enum mh_error_code
read_track(const char *path, struct mh_track *track, struct mh_error *error_out)
{
	struct mh_file config;
	unsigned char *text;
	size_t size;
	enum mh_error_code status;

	status = mh_file_read(path, &config, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_tbl_decode(
		config.data, config.size, &text, &size, error_out);
	free(config.data);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_track_config_trk_decode(text, size, track, error_out);
	free(text);

	return status;
}

static enum mh_error_code
sample_cells(const struct mh_myw *world, const struct mh_track *track,
	struct mh_prepared_light *prepared, struct mh_error *error_out)
{
	unsigned long n;
	double position[3], eye[3];
	enum mh_error_code status;

	/* Probe cell centers above the floor, using an off-axis eye. */
	for (n = 0; n < world->cell_count; n++) {
		position[0] = world->grid_minimum[0] +
			(n % world->width + 0.5) * world->cell_size[0];
		position[1] = world->grid_minimum[1] + 5.0;
		position[2] = world->grid_minimum[2] +
			(n / world->width + 0.5) * world->cell_size[1];
		eye[0] = position[0] + 7;
		eye[1] = position[1] + 11;
		eye[2] = position[2] + 13;
		status = sample_lighting(world, track, prepared, position, eye,
			NULL, n, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct mh_myw world;
	struct mh_track track;
	struct mh_prepared_light *prepared;
	struct mh_error error;
	enum mh_error_code status;
	size_t link_count;
	unsigned int i;
	double query[9];
	char *end;

	if (argc != 2 && argc != 3 && argc != 12) {
		fprintf(stderr,
			"Usage: mh-light-info WORLD.myw [TRACK.trk [X Y Z NX NY NZ EX EY EZ]]\n"
			"With a track: sample six normals per cell, or one supplied position/normal/eye.\n"
			"RGB is the lighting result before material multiplication and clamping.\n");
		return 1;
	}

	for (i = 0; argc == 12 && i < 9; i++) {
		errno = 0;
		query[i] = strtod(argv[3 + i], &end);

		if (errno != 0 || end == argv[3 + i] || *end != 0 ||
			!(query[i] >= -QUERY_COORDINATE_LIMIT &&
				query[i] <= QUERY_COORDINATE_LIMIT)) {
			fprintf(stderr,
				"Query values must be finite and in [-1e6,1e6].\n");
			return 1;
		}
	}

	status = mh_file_read(argv[1], &file, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	prepared = NULL;
	status = mh_myw_parse(file.data, file.size, &world, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	print_world_lights(&world);

	if (argc == 2)
		goto done;

	status = read_track(argv[2], &track, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	printf("settings\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\n",
		track.object_brightness[0], track.object_brightness[1],
		track.object_brightness[2], track.object_ambient[0],
		track.object_ambient[1], track.object_ambient[2],
		track.light_intensity);

	/*
	 * Nodes, not unique lights: an acyclic list may repeat a light index.
	 */
	link_count = (size_t)world.blocks[MH_MYW_LINKS].count;

	if (link_count > (size_t)-1 / sizeof(*prepared)) {
		status = mh_error_set(
			&error, MH_ERROR_RANGE, "too many light links");
		goto done;
	}

	if (link_count != 0) {
		prepared = (struct mh_prepared_light *)malloc(
			link_count * sizeof(*prepared));

		if (prepared == NULL) {
			status = mh_error_set(&error, MH_ERROR_MEMORY,
				"out of memory preparing lights");
			goto done;
		}
	}

	if (argc == 12) {
		status = sample_lighting(&world, &track, prepared, query,
			query + 6, query + 3, 0, &error);
		goto done;
	}

	status = sample_cells(&world, &track, prepared, &error);

done:
	free(prepared);
	free(file.data);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	return status == MH_ERROR_NONE && fflush(stdout) == 0 && !ferror(stdout)
		? 0
		: 1;
}
