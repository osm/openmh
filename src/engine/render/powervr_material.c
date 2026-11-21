#include <string.h>

#include "engine/render/powervr_effect.h"
#include "engine/render/powervr_material.h"

enum {
	QUAD_VERTICES = 6
};

#define COLOR_NORMALIZER 255.0f

#define POWERVR_ALPHA_MAX 15U
#define POWERVR_ALPHA_STEP 16U
#define POWERVR_ALPHA_AVERAGE_SCALE 510U
#define POWERVR_ALPHA_AVERAGE_DIVISOR 8192U
/* Packed alpha 0x4f, reduced to four-bit opacity by PowerSGL. */
#define POWERVR_SKID_TRANSPARENCY 11.0f
/* Packed alpha 0x80, hence four-bit transparency 15 - 8. */
#define POWERVR_UNAVAILABLE_TRANSPARENCY 7.0f

#define POWERVR_ALPHA_SHIFT 28

/*
 * Red is zero after a shadow/particle quad. Green/blue are flat first-corner
 * values.
 */
static void
prepare_unavailable_color(
	struct mh_vertex *vertex, const struct mh_scene_vertex *first)
{
	vertex->color[0] = 0;

	vertex->color[1] = first->rgb[1] / COLOR_NORMALIZER;
	vertex->color[2] = first->rgb[2] / COLOR_NORMALIZER;
	vertex->opacity = POWERVR_UNAVAILABLE_TRANSPARENCY;
}

void
mh_powervr_face_color_prepare(struct mh_scene_face *face, int smooth)
{
	unsigned int background, i;

	if (face->pass >= MH_SCENE_SOLID)
		return;

	background = face->corners[0].background;

	if (face->pass == MH_SCENE_DISTANT_ALPHA && smooth) {
		background = 0;

		for (i = 0; i < face->count; i++)
			background += face->corners[i].background;

		background /= face->count;
	}

	for (i = 0; i < face->count; i++) {
		face->corners[i].background = background;

		if (i != 0 && face->pass == MH_SCENE_DISTANT_SOLID)
			memcpy(face->corners[i].rgb, face->corners[0].rgb,
				sizeof(face->corners[i].rgb));
	}
}

void
mh_powervr_vertex_color_prepare(struct mh_vertex *vertex,
	const struct mh_scene_face *face, unsigned int corner,
	enum mh_scene_effect effect, size_t list)
{
	unsigned int background;

	if (effect == MH_SCENE_EFFECT_UNAVAILABLE)
		prepare_unavailable_color(vertex, &face->corners[0]);

	if (effect == MH_SCENE_EFFECT_SKIDS) {
		memset(vertex->color, 0, sizeof(vertex->color));
		vertex->opacity = POWERVR_SKID_TRANSPARENCY;
	}

	/* SGL adds four-bit vertex and texture transparency. */
	if (list < MH_LIST_DISTANT_COUNT) {
		background = 255 - face->corners[corner].background;
		background = list == MH_LIST_DISTANT_SOLID
			? background / POWERVR_ALPHA_STEP
			: background * POWERVR_ALPHA_AVERAGE_SCALE /
				POWERVR_ALPHA_AVERAGE_DIVISOR;
		vertex->opacity = (float)(POWERVR_ALPHA_MAX - background);
	}
}

enum mh_error_code
mh_powervr_effect_colors_prepare(const struct mh_mesh *mesh,
	struct mh_vertex *vertices, enum mh_scene_effect effect,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned long packed;
	size_t first, i;

	if (mesh->material_rgb == NULL ||
		mesh->material_rgb_count != mesh->vertex_count ||
		mesh->vertex_count % QUAD_VERTICES != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"PowerVR effects require raw quad colors");

	for (first = 0; first < mesh->vertex_count; first += QUAD_VERTICES) {
		status = effect == MH_SCENE_EFFECT_HALOS
			? mh_powervr_halo_color(mesh->material_rgb + first * 3,
				&packed, error_out)
			: mh_powervr_particle_color(
				mesh->material_rgb + first * 3, &packed,
				error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = first; i < first + QUAD_VERTICES; i++) {
			vertices[i].color[0] =
				((packed >> 16) & 255) / COLOR_NORMALIZER;
			vertices[i].color[1] =
				((packed >> 8) & 255) / COLOR_NORMALIZER;
			vertices[i].color[2] =
				(packed & 255) / COLOR_NORMALIZER;

			/*
			 * SGL flat opacity is the top nibble, inverted for
			 * blending.
			 */
			vertices[i].opacity = (float)(POWERVR_ALPHA_MAX -
				(packed >> POWERVR_ALPHA_SHIFT));
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
