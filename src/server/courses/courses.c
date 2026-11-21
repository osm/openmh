#include <string.h>

#include "engine/core/text.h"
#include "interfaces/navigation/source.h"
#include "server/courses/courses.h"

#define COURSE_BYTES (16UL * 1024 * 1024)

void
mh_server_courses_free(struct mh_server_courses *courses)
{
	unsigned int i;

	for (i = 0; i < courses->count; i++)
		mh_route_free(&courses->routes[i]);

	memset(courses, 0, sizeof(*courses));
}

enum mh_error_code
mh_server_courses_load(const struct mh_navigation_source *navigation_source,
	const struct mh_resource_source *source, const char *root,
	const struct mh_server_config *config, struct mh_server_courses *output,
	unsigned int *failed_track, struct mh_error *error_out)
{
	struct mh_server_courses courses;
	unsigned int i;
	enum mh_error_code status;

	if (source == NULL || root == NULL || config == NULL ||
		output == NULL || config->track_count == 0 ||
		config->track_count > MH_SERVER_TRACKS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"no configured server courses");

	memset(&courses, 0, sizeof(courses));
	courses.selected = config->track_count;

	for (i = 0; i < config->track_count; i++) {
		if (mh_ascii_equal(config->tracks[i].name, config->start_track))
			courses.selected = i;

		status = mh_route_load_bounded_from(navigation_source, source,
			root, config->tracks[i].path, COURSE_BYTES,
			&courses.routes[i], error_out);

		if (status != MH_ERROR_NONE) {
			if (failed_track != NULL)
				*failed_track = i;

			mh_server_courses_free(&courses);

			return status;
		}

		courses.count++;
	}

	if (courses.selected == courses.count) {
		mh_server_courses_free(&courses);
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"StartTrack is not in the configured course list");
	}

	*output = courses;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
