#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/navigation_source.h"
#include "backends/navigation/provider.h"
#include "engine/assets/tbl.h"
#include "engine/navigation/ai_path.h"
#include "engine/navigation/camera_motion.h"
#include "engine/navigation/route.h"
#include "server/courses/courses.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_original_routes(void)
{
	static const unsigned char text[] = "LWMO\n1\n9\n3\n"
					"0 0 0 0 0 0 1 1 1\n0 1 0 0 0\n"
					"10 0 0 0 0 0 1 1 1\n1 1 0 0 0\n"
					"0 0 0 0 0 0 1 1 1\n2 1 0 0 0\n";
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *resources;
	struct mh_route route, before;
	struct mh_camera_motion camera;
	unsigned char *wrapped;
	size_t size;

	memset(&route, 0, sizeof(route));
	memset(&camera, 0, sizeof(camera));
	before = route;
	CHECK(mh_tbl_encode_named(text, sizeof(text) - 1, NULL, &wrapped, &size,
		NULL) == MH_ERROR_NONE);
	entries[0].path = "memory/wrapped.mot";
	entries[0].data = wrapped;
	entries[0].size = size;
	entries[1].path = "memory/plain.mot";
	entries[1].data = text;
	entries[1].size = sizeof(text) - 1;
	CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_route_load_bounded_from(mh_navigation_tbl_routes(), resources,
		"memory", "wrapped.mot", size - 1, &route,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&route, &before, sizeof(route)) == 0);
	CHECK(mh_route_load_bounded_from(mh_navigation_tbl_routes(), resources,
		"memory", "wrapped.mot", size, &route,
		NULL) == MH_ERROR_NONE);
	CHECK(route.motion.key_count == 3 && route.motion.keys[2].time == 2);
	mh_route_free(&route);
	CHECK(mh_route_load_from(mh_navigation_tbl_routes(), resources,
		"memory", "plain.mot", &route, NULL) != MH_ERROR_NONE);
	CHECK(mh_route_load_from(mh_navigation_mot_dat_source(), resources,
		"memory", "plain.mot", &route, NULL) == MH_ERROR_NONE);
	CHECK(mh_camera_motion_load_from(mh_navigation_tbl_routes(), resources,
		"memory", "plain.mot", &camera,
		NULL) == MH_ERROR_UNSUPPORTED);
	mh_route_free(&route);
	mh_memory_resources_close(resources);
	free(wrapped);

	return 0;
}

static int
check_selected_sources(void)
{
	static const unsigned char kinds[] = {
		MH_TEST_ROUTE, MH_TEST_CAMERA, MH_TEST_AI};

	static const struct mh_resource_entry entries[] = {
		{"memory/route.fixturenav", &kinds[0], 1},
		{"memory/camera.fixturenav", &kinds[1], 1},
		{"memory/ai.fixturenav", &kinds[2], 1}};

	struct mh_test_navigation_source source;
	struct mh_navigation_source provider;
	const struct mh_resource_source *resources;
	struct mh_route route, before;
	struct mh_camera_motion camera;
	struct mh_ai_path ai;
	struct mh_server_courses courses, saved;
	struct mh_server_config config;
	double channels[9];
	size_t i;
	unsigned int failed;

	memset(&source, 0, sizeof(source));
	memset(&route, 0, sizeof(route));
	memset(&camera, 0, sizeof(camera));
	memset(&ai, 0, sizeof(ai));
	memset(&courses, 0, sizeof(courses));
	memset(&config, 0, sizeof(config));
	mh_test_navigation_source_init(&source, &provider);

	/* Authored runtime values, with no original file encoding. */
	source.route.motion.keys = calloc(3, sizeof(*source.route.motion.keys));
	source.camera.keys = calloc(1, sizeof(*source.camera.keys));
	source.ai.points = calloc(2, sizeof(*source.ai.points));
	source.ai.sections = calloc(1, sizeof(*source.ai.sections));
	CHECK(source.route.motion.keys && source.camera.keys &&
		source.ai.points && source.ai.sections);
	source.route.motion.key_count = 3;
	source.route.motion.end_behavior = MH_SCENE_REPEAT;
	source.route.motion.keys[1].time = 1;
	source.route.motion.keys[1].linear = 1;
	source.route.motion.keys[1].value[0] = 10;
	source.route.motion.keys[2].time = 2;
	source.camera.count = 1;
	source.camera.keys[0].selector = 3;
	source.camera.keys[0].focal = 2.5f;
	source.ai.count = 2;
	source.ai.section_count = 1;
	source.ai.points[1].x = 42;
	source.ai.points[0].direction[2] = 1;
	source.ai.points[1].direction[2] = 1;
	source.ai.sections[0].count = 2;

	for (i = 0; i < MH_TEST_NAVIGATION_KINDS; i++)
		source.present[i] = 1;

	CHECK(mh_memory_resources_open(entries, 3, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_route_load_from(&provider, resources, "memory", "ROUTE.mot",
		&route, NULL) == MH_ERROR_NONE);
	CHECK(source.route_limit == MH_RESOURCE_READ_LIMIT);
	CHECK(route.motion.keys != source.route.motion.keys);
	CHECK(mh_camera_motion_load_from(&provider, resources, "memory",
		"Camera.mot", &camera, NULL) == MH_ERROR_NONE);
	CHECK(mh_ai_path_load_from(&provider, resources, "memory", "ai.dat",
		&ai, NULL) == MH_ERROR_NONE);
	before = route;
	CHECK(mh_route_load_bounded_from(&provider, resources, "memory",
		"route.mot", 0, &route, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&route, &before, sizeof(route)) == 0);
	CHECK(mh_route_load_from(&provider, resources, "memory", "missing.mot",
		&route, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&route, &before, sizeof(route)) == 0);
	CHECK(mh_route_load_from(NULL, resources, "memory", "route.mot", &route,
		NULL) == MH_ERROR_UNSUPPORTED);

	config.track_count = 2;
	strcpy(config.tracks[0].name, "First");
	strcpy(config.tracks[1].name, "Second");
	strcpy(config.tracks[0].path, "route.mot");
	strcpy(config.tracks[1].path, "route.mot");
	strcpy(config.start_track, "Second");
	CHECK(mh_server_courses_load(&provider, resources, "memory", &config,
		&courses, &failed, NULL) == MH_ERROR_NONE);
	CHECK(source.route_limit == 16UL * 1024 * 1024);
	CHECK(courses.count == 2 && courses.selected == 1);
	CHECK(courses.routes[0].motion.keys != courses.routes[1].motion.keys);
	saved = courses;
	strcpy(config.tracks[1].path, "missing.mot");
	failed = 99;
	CHECK(mh_server_courses_load(&provider, resources, "memory", &config,
		&courses, &failed, NULL) != MH_ERROR_NONE);
	CHECK(failed == 1 && memcmp(&saved, &courses, sizeof(courses)) == 0);
	mh_memory_resources_close(resources);
	mh_test_navigation_source_free(&source);

	CHECK(mh_scene_channels(&route.motion, 0.5, channels, NULL) ==
		MH_ERROR_NONE);
	CHECK(channels[0] == 5);
	CHECK(camera.keys[0].selector == 3 && camera.keys[0].focal == 2.5f);
	CHECK(ai.points[1].x == 42 && ai.sections[0].count == 2);
	mh_server_courses_free(&courses);
	mh_route_free(&route);
	mh_camera_motion_free(&camera);
	mh_ai_path_free(&ai);

	return 0;
}

int
main(void)
{
	CHECK(check_original_routes() == 0);
	CHECK(check_selected_sources() == 0);
	puts("Navigation sources: renamed assets, owned data, original wrappers and bounded server loading pass");

	return 0;
}
