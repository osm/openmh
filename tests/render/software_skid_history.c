#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/render/software_skid_history.h"

/*
 * Focal length, bounds, camera and six planes; then XYZ endpoints/visibility.
 */

struct capture {
	const struct mh_skid_wheel	*wheel;
	unsigned int	count, indices[MH_SKID_SEGMENTS + 1], fail_at;
};

static enum mh_error_code
capture_segment(void *context, const struct mh_skid_segment *segment,
	int *visible, struct mh_error *error_out)
{
	struct capture *capture;

	capture = (struct capture *)context;

	if (capture->count == capture->fail_at)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "fixture failure");

	if (capture->count >= MH_SKID_SEGMENTS + 1)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many segments");

	capture->indices[capture->count++] =
		(unsigned int)(segment - capture->wheel->segments);
	*visible = segment->normal[0] != 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(void)
{
	struct mh_skid_wheel wheel;
	struct mh_world_view view;
	struct capture capture;
	struct mh_error error;
	unsigned int i;
	int visible;

	memset(&wheel, 0, sizeof(wheel));
	memset(&view, 0, sizeof(view));
	memset(&capture, 0, sizeof(capture));
	view.profile = MH_RENDER_SOFTWARE;
	view.bounds[0] = view.bounds[2] = -100;
	view.bounds[1] = view.bounds[3] = 100;
	capture.wheel = &wheel;
	capture.fail_at = MH_SKID_SEGMENTS + 1;
	wheel.first = 49;
	wheel.count = 50;
	wheel.partial = 1;
	wheel.segments[49].normal[0] = 1;

	if (mh_software_skid_visit(&wheel, &view, 1, capture_segment, &capture,
		&visible, NULL) != MH_ERROR_NONE ||
		visible != 1 || capture.count != 51)
		return 1;

	for (i = 0; i < 51; i++)
		if (capture.indices[i] != (49 + i) % 50)
			return 1;

	/* A late preparation failure must not publish earlier visibility. */
	capture.count = 0;
	capture.fail_at = 30;
	visible = 123;

	if (mh_software_skid_visit(&wheel, &view, 1, capture_segment, &capture,
		&visible, &error) != MH_ERROR_MEMORY ||
		visible != 123 || error.message == NULL || capture.count != 30)
		return 1;

	/* Reject a late invalid position before invoking any callback. */
	capture.count = 0;
	wheel.segments[35].end[0] = FLT_MAX;

	if (mh_software_skid_visit(&wheel, &view, 1, capture_segment, &capture,
		&visible, NULL) != MH_ERROR_INVALID ||
		visible != 123 || capture.count != 0)
		return 1;

	wheel.count = 0;
	wheel.partial = 0;
	wheel.oldest_visible = 1;

	if (mh_software_skid_visit(&wheel, &view, 1, capture_segment, &capture,
		&visible, NULL) != MH_ERROR_NONE ||
		visible != 1 || capture.count != 0)
		return 1;

	wheel.first = MH_SKID_SEGMENTS;
	visible = 123;

	if (mh_software_skid_visit(&wheel, &view, 1, capture_segment, &capture,
		&visible, NULL) != MH_ERROR_INVALID ||
		visible != 123)
		return 1;

	wheel.first = 0;
	view.profile = MH_RENDER_GLIDE;

	if (mh_software_skid_visit(&wheel, &view, 1, capture_segment, &capture,
		&visible, NULL) != MH_ERROR_INVALID ||
		visible != 123)
		return 1;

	return 0;
}
