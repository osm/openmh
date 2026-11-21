#include <stdlib.h>
#include <string.h>

#include "backends/render/software_light_select.h"
#include "backends/render/software_skid_light.h"
#include "backends/render/software_skids.h"
#include "engine/render/software_skid.h"
#include "engine/render/software_skid_history.h"

#define INITIAL_FACES 64
#define CAMERA_LIMIT 1e9f

struct skid_prepare {
	struct mh_software_skids		*output;
	const struct mh_software_skid_view	*view;
};

void
mh_software_skids_free(struct mh_software_skids *state)
{
	free(state->faces);
	free(state->lights);
	memset(state, 0, sizeof(*state));
}

static enum mh_error_code
select_color(struct skid_prepare *context, const struct mh_software_skid *skid,
	const float material[3], float color[3], struct mh_error *error_out)
{
	struct mh_software_skids *state;
	const struct mh_software_skid_view *view;
	struct mh_model_lighting lighting;
	struct mh_point_light *lights;
	size_t capacity;
	float sum;
	enum mh_error_code status;

	state = context->output;
	view = context->view;
	sum = (float)((double)material[0] + material[1]);
	sum = (float)((double)sum + material[2]);

	/*
	 * The original black-mark bypass does not query the world light grid.
	 */
	if (sum <= 1 || view->world == NULL)
		return mh_software_skid_color(view->lighting, skid->sample,
			material, color, error_out);

	if (view->lighting == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software skid world lighting");

	if (view->world->link_count > (size_t)-1 / sizeof(*state->lights))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software skid light capacity overflow");

	capacity = (size_t)view->world->link_count;

	if (capacity > state->light_capacity) {
		lights = realloc(state->lights, capacity * sizeof(*lights));

		if (lights == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory preparing skid lights");

		state->lights = lights;
		state->light_capacity = capacity;
	}

	lighting = *view->lighting;
	lighting.lights = state->lights;
	status = mh_software_skid_light_select(view->world, skid->sample,
		state->lights, state->light_capacity, &lighting.light_count,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_software_skid_color(
		&lighting, skid->sample, material, color, error_out);
}

static enum mh_error_code
append_face(struct mh_software_skids *state,
	const struct mh_software_skid_face *face, struct mh_error *error_out)
{
	struct mh_software_skid_face *faces;
	size_t capacity, limit;

	if (face->count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (state->count == state->capacity) {
		limit = (size_t)-1 / sizeof(*faces);

		if (state->capacity >= limit)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software skid face capacity overflow");

		if (state->capacity == 0)
			capacity = INITIAL_FACES;
		else if (state->capacity > limit / 2)
			capacity = limit;
		else
			capacity = state->capacity * 2;

		faces = realloc(state->faces, capacity * sizeof(*faces));

		if (faces == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory preparing skid faces");

		state->faces = faces;
		state->capacity = capacity;
	}

	state->faces[state->count++] = *face;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_segment(void *opaque, const struct mh_skid_segment *segment,
	int *visible, struct mh_error *error_out)
{
	static const float black[3] = {0, 0, 0};
	struct skid_prepare *context;
	struct mh_software_skid skid;
	struct mh_software_skid_face face;
	float positions[12], color[3];
	unsigned int quad, i, triangle, corner;
	int shaded;
	enum mh_error_code status;

	context = (struct skid_prepare *)opaque;
	status = mh_software_skid_prepare(segment, &skid, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	shaded = 0;
	*visible = 0;

	for (quad = 0; quad < skid.quads; quad++) {
		mh_software_skid_pair(&skid, quad, positions, positions + 9);
		mh_software_skid_pair(
			&skid, quad + 1, positions + 3, positions + 6);
		status = mh_software_skid_face_prepare(
			context->view->projection, context->view->camera,
			positions, black, context->view->cue, &face, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!face.visible)
			continue;

		*visible = 1;

		if (!shaded) {
			status = select_color(context, &skid, segment->color,
				color, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			for (i = 0; i < 3; i++)
				if (!(color[i] >= -MH_SOFTWARE_LIGHT_MAX &&
					color[i] <= MH_SOFTWARE_LIGHT_MAX))
					return mh_error_set(error_out,
						MH_ERROR_RANGE,
						"invalid software skid face color");

			shaded = 1;
		}

		/*
		 * Flat RGB is shared by the entire segment, including clipped
		 * faces.
		 */
		for (triangle = 0; triangle < face.count; triangle++)
			for (corner = 0; corner < 3; corner++)
				memcpy(face.triangles[triangle].color[corner],
					color, sizeof(color));

		status = append_face(context->output, &face, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_skids_prepare(struct mh_software_skids *state,
	const struct mh_skids *histories, unsigned int cars,
	const struct mh_software_skid_view *view, struct mh_error *error_out)
{
	struct skid_prepare context;
	int feedback[MH_SOFTWARE_SKID_CARS][4];
	unsigned int car, wheel, i;
	enum mh_error_code status;

	if (state == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software skid scratch");

	state->count = 0;

	if (histories == NULL || cars == 0 || cars > MH_SOFTWARE_SKID_CARS ||
		view == NULL || view->projection == NULL ||
		view->camera == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software skid preparation");

	status = mh_software_projection_check(view->projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 12; i++)
		if (!(view->camera[i] >= -CAMERA_LIMIT &&
			view->camera[i] <= CAMERA_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software skid camera");

	context.output = state;
	context.view = view;
	memset(feedback, 0, sizeof(feedback));

	for (car = 0; car < cars; car++)
		for (wheel = 0; wheel < 4; wheel++) {
			status = mh_software_skid_visit(
				&histories[car].wheels[wheel], view->world_view,
				view->projection->clip.focal, prepare_segment,
				&context, &feedback[car][wheel], error_out);

			if (status != MH_ERROR_NONE) {
				state->count = 0;
				return status;
			}
		}

	memcpy(state->oldest_visible, feedback, sizeof(feedback));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
