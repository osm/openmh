#include <stddef.h>

#include "engine/render/software_skid_history.h"

#define POSITION_LIMIT 1000000
#define VIEW_LIMIT 1e9f
#define FOCAL_MIN .5f
#define FOCAL_MAX 10
#define DISTANCE_SCALE 70
#define START_RADIUS 10

static int
valid_view(const struct mh_world_view *view, float focal)
{
	unsigned int i, j;

	if (view == NULL || view->profile != MH_RENDER_SOFTWARE ||
		!(focal >= FOCAL_MIN && focal <= FOCAL_MAX))
		return 0;

	for (i = 0; i < 3; i++)
		if (!(view->camera[i] >= -POSITION_LIMIT &&
			view->camera[i] <= POSITION_LIMIT))
			return 0;

	for (i = 0; i < 4; i++)
		if (!(view->bounds[i] >= -VIEW_LIMIT &&
			view->bounds[i] <= VIEW_LIMIT))
			return 0;

	for (i = 0; i < MH_FRUSTUM_PLANES; i++)
		for (j = 0; j < 4; j++)
			if (!(view->frustum.planes[i][j] >= -VIEW_LIMIT &&
				view->frustum.planes[i][j] <= VIEW_LIMIT))
				return 0;

	return 1;
}

static int
valid_positions(const struct mh_skid_segment *segment)
{
	unsigned int i;

	for (i = 0; i < 3; i++)
		if (!(segment->start[i] >= -POSITION_LIMIT &&
			segment->start[i] <= POSITION_LIMIT &&
			segment->end[i] >= -POSITION_LIMIT &&
			segment->end[i] <= POSITION_LIMIT))
			return 0;

	return 1;
}

static int
segment_visible(const struct mh_skid_segment *segment,
	const struct mh_world_view *view, float radius_squared)
{
	float delta, squared[3], distance_squared;
	unsigned int i;

	/* Touching any expanded X/Z boundary rejects the segment. */
	if (!(segment->start[0] > view->bounds[0] &&
		segment->start[0] < view->bounds[1] &&
		segment->start[2] > view->bounds[2] &&
		segment->start[2] < view->bounds[3]))
		return 0;

	if (!mh_frustum_sphere_profile(MH_RENDER_SOFTWARE, &view->frustum,
		segment->start, START_RADIUS))
		return 0;

	/* Squared endpoint distance, with binary32 operations. */
	for (i = 0; i < 3; i++) {
		delta = (float)((double)segment->end[i] - view->camera[i]);
		squared[i] = (float)((double)delta * delta);
	}

	distance_squared = (float)((double)squared[1] + squared[0]);
	distance_squared = (float)((double)distance_squared + squared[2]);

	return distance_squared < radius_squared;
}

enum mh_error_code
mh_software_skid_visit(const struct mh_skid_wheel *wheel,
	const struct mh_world_view *view, float focal,
	enum mh_error_code (*prepare)(void *, const struct mh_skid_segment *,
		int *, struct mh_error *),
	void *context, int *oldest_visible, struct mh_error *error_out)
{
	const struct mh_skid_segment *segment;
	float radius, radius_squared;
	unsigned int offset, count;
	int oldest, visible;
	enum mh_error_code status;

	if (wheel == NULL || prepare == NULL || oldest_visible == NULL ||
		!valid_view(view, focal) || wheel->count > MH_SKID_SEGMENTS ||
		wheel->first >= MH_SKID_SEGMENTS ||
		(wheel->partial != 0 && wheel->partial != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software skid history");

	count = wheel->count + (unsigned int)wheel->partial;

	for (offset = 0; offset < count; offset++)
		if (!valid_positions(&wheel->segments[(wheel->first + offset) %
			MH_SKID_SEGMENTS]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software skid history position");

	radius = (float)((double)focal * DISTANCE_SCALE);
	radius_squared = (float)((double)radius * radius);
	oldest = count == 0 ? wheel->oldest_visible : 0;

	for (offset = 0; offset < count; offset++) {
		segment = &wheel->segments[(wheel->first + offset) %
			MH_SKID_SEGMENTS];

		if (!segment_visible(segment, view, radius_squared))
			continue;

		visible = 0;
		status = prepare(context, segment, &visible, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (offset == 0)
			oldest = visible != 0;
	}

	*oldest_visible = oldest;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
