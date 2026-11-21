#include "engine/render/tron_draw.h"

#define RECIPROCAL_W_MIN 1e-20f

enum mh_error_code
mh_tron_draw_face(const struct mh_renderer_tron_face *input, unsigned int width,
	unsigned int height, struct mh_tron_draw *draw,
	struct mh_tron_vertex vertices[MH_TRON_FACE_VERTICES_MAX],
	struct mh_error *error_out)
{
	static const unsigned int triangles[MH_TRON_FILL_VERTICES_MAX] = {
		0, 1, 2, 0, 2, 3};

	struct mh_tron_face face;
	struct mh_tron_line_vertex lines[MH_TRON_OUTLINE_VERTICES_MAX];
	enum mh_error_code status;
	unsigned int line_count, j, used;

	if (input == NULL || draw == NULL || vertices == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing TRON face output");

	used = 0;

	status = mh_tron_prepare_face(input->pass, input->count, input->smooth,
		input->corners, &face, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_tron_face_outline(
			&face, width, height, lines, &line_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (j = 0; j < face.count; j++)
		if (face.vertices[j].line.reciprocal_w < RECIPROCAL_W_MIN)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"TRON reciprocal W exceeds native projection range");

	draw->pass = face.pass;
	draw->fill_first = used;
	draw->fill_count = face.count == 4 ? MH_TRON_FILL_VERTICES_MAX : 3;

	for (j = 0; j < draw->fill_count; j++)
		vertices[used++] = face.vertices[triangles[j]];

	draw->outline_first = used;
	draw->outline_count = line_count * 2;

	for (j = 0; j < line_count * 2; j++) {
		vertices[used].line = lines[j];

		/*
		 * Original Z-buffered, untextured lines do not consume
		 * W. Retain screen-linear attributes even if the
		 * original clipper extrapolated its otherwise unused
		 * reciprocal-W field.
		 */
		vertices[used].line.reciprocal_w = 1;
		vertices[used].texture_w[0] = vertices[used].texture_w[1] = 0;
		used++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
