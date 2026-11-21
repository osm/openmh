#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/myw.h"
#include "engine/audio/a3d20_geometry.h"
#include "engine/geometry/world.h"

struct geometry_capture {
	int				printing;
	unsigned int			begins, ends, triangles;
	struct mh_a3d20_triangle	last;
};

static void
begin(void *context)
{
	struct geometry_capture *output;

	output = context;
	output->begins++;
}

static void
triangle(void *context, const struct mh_a3d20_triangle *value)
{
	struct geometry_capture *output;
	unsigned int vertex, axis;

	output = context;
	output->triangles++;
	output->last = *value;

	if (!output->printing)
		return;

	printf(" %lu", (unsigned long)value->tag);

	for (vertex = 0; vertex < 3; vertex++)
		for (axis = 0; axis < 3; axis++)
			printf(" %.9g", (double)value->vertices[vertex][axis]);
}

static void
end(void *context)
{
	struct geometry_capture *output;

	output = context;
	output->ends++;
}

static void
setup_sink(struct mh_a3d20_geometry_sink *sink, struct geometry_capture *output)
{
	memset(output, 0, sizeof(*output));
	sink->context = output;
	sink->begin = begin;
	sink->triangle = triangle;
	sink->end = end;
}

static int
check_submission(void)
{
	struct mh_a3d20_geometry geometry;
	struct mh_a3d20_geometry_sink sink;
	struct mh_a3d20_material material;
	struct geometry_capture output;
	unsigned int vertex;

	setup_sink(&sink, &output);
	memset(&geometry, 0, sizeof(geometry));
	mh_a3d20_geometry_submit(&geometry, &sink);

	if (output.begins != 1 || output.ends != 1 || output.triangles != 0)
		return 0;

	geometry.count = 1;
	geometry.triangles[0].tag = 123;

	for (vertex = 0; vertex < 3; vertex++) {
		geometry.triangles[0].vertices[vertex][0] = (float)vertex;
		geometry.triangles[0].vertices[vertex][1] = (float)vertex + 4;
		geometry.triangles[0].vertices[vertex][2] = (float)vertex - 1;
	}

	mh_a3d20_geometry_submit(&geometry, &sink);

	if (output.begins != 2 || output.ends != 2 || output.triangles != 1 ||
		output.last.tag != 123)
		return 0;

	for (vertex = 0; vertex < 3; vertex++)
		if (output.last.vertices[vertex][0] != (float)vertex ||
			output.last.vertices[vertex][1] != (float)vertex + 4 ||
			output.last.vertices[vertex][2] != 1 - (float)vertex)
			return 0;

	mh_a3d20_material_defaults(&material);

	return material.transmittance[0] == (float).3 &&
		material.transmittance[1] == (float).3 &&
		material.reflectance[0] == (float).8 &&
		material.reflectance[1] == (float).9 &&
		material.reflection_delay == (float).1 &&
		material.reflection_gain == (float)1.2;
}

int
main(void)
{

	if (!check_submission()) {
		fputs("A3D 2.0 geometry submission failed\n", stderr);
		return 1;
	}

	puts("A3D 2.0 material and geometry submission passed");

	return 0;
}
