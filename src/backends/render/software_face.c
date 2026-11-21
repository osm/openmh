#include <string.h>

#include "backends/render/software_face.h"
#include "backends/render/software_perspective.h"

#define FACE_FLAGS                                                             \
	(MH_NEAR_QUAD | MH_NEAR_TEXTURED | MH_NEAR_VERTEX_COLOR |              \
		MH_SOFTWARE_FACE_PERSPECTIVE | MH_SOFTWARE_FACE_ADDITIVE |     \
		MH_SOFTWARE_FACE_CUTOUT | MH_SOFTWARE_FACE_OUTLINE)

#define COLOR_BYTE_MAX 255
#define PERSPECTIVE_SHIFT_MIN 2
#define PERSPECTIVE_SHIFT_MAX 5

/* Saturate smooth RGB after near clipping. */
static enum mh_error_code
clamp_smooth(float color[3], struct mh_error *error_out)
{
	unsigned int channel;

	for (channel = 0; channel < 3; channel++) {
		if (!(color[channel] >= 0 &&
			color[channel] <= MH_SOFTWARE_LIGHT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"invalid software face light");

		if (color[channel] > COLOR_BYTE_MAX)
			color[channel] = COLOR_BYTE_MAX;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_face_prepare(const struct mh_software_clipped_face *face,
	const struct mh_software_material *material,
	struct mh_software_triangle output[2], unsigned int *count,
	struct mh_error *error_out)
{
	static const unsigned int corners[2][3] = {{0, 2, 3}, {0, 1, 2}};
	struct mh_software_triangle prepared[2], *triangle;
	const struct mh_software_clipped_corner *vertex;
	unsigned int first, i, j, color_corner;
	int textured;
	enum mh_error_code status;

	if (face == NULL || material == NULL || output == NULL || count == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software face inputs");

	if ((face->flags & ~(unsigned int)FACE_FLAGS) != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported software face flags");

	if (face->count != (face->flags & MH_NEAR_QUAD ? 4U : 3U) ||
		material->mapping < MH_TEXTURE_AFFINE ||
		material->mapping > MH_TEXTURE_PERSPECTIVE ||
		material->perspective_shift < PERSPECTIVE_SHIFT_MIN ||
		material->perspective_shift > PERSPECTIVE_SHIFT_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software face shape or mapping policy");

	textured = (face->flags & MH_NEAR_TEXTURED) != 0;

	if (textured != (material->texture != NULL) ||
		(!textured && (face->flags & MH_SOFTWARE_FACE_ADDITIVE)) ||
		((face->flags & MH_SOFTWARE_FACE_CUTOUT) != 0) !=
			(textured && material->texture->zero_transparent != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"software face texture does not match its flags");

	memset(prepared, 0, sizeof(prepared));
	first = face->count == 4 ? 0 : 1;

	for (i = first; i < 2; i++) {
		triangle = &prepared[i - first];
		triangle->texture = material->texture;
		triangle->additive =
			(face->flags & MH_SOFTWARE_FACE_ADDITIVE) != 0;
		triangle->fog = material->fog;
		memcpy(triangle->fog_color, material->fog_color,
			sizeof(triangle->fog_color));
		triangle->smooth =
			!textured && (face->flags & MH_NEAR_VERTEX_COLOR);
		triangle->outline =
			(face->flags & MH_SOFTWARE_FACE_OUTLINE) != 0;

		for (j = 0; j < 3; j++) {
			vertex = &face->corners[corners[i][j]];
			memcpy(triangle->xy[j], vertex->xy, sizeof(vertex->xy));
			memcpy(triangle->uv[j], vertex->uv, sizeof(vertex->uv));
			triangle->perspective_weight[j] = vertex->weight;
			color_corner = triangle->smooth ? corners[i][j] : 0;
			memcpy(triangle->color[j],
				face->corners[color_corner].rgb,
				sizeof(triangle->color[j]));

			if (triangle->smooth) {
				status = clamp_smooth(
					triangle->color[j], error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}
		}

		if (textured && !triangle->additive &&
			!material->texture->zero_transparent &&
			mh_software_perspective_needed(material->mapping,
				(face->flags & MH_SOFTWARE_FACE_PERSPECTIVE) !=
					0,
				triangle->perspective_weight))
			triangle->perspective_shift =
				material->perspective_shift;
	}

	memcpy(output, prepared, (2 - first) * sizeof(*output));
	*count = 2 - first;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
