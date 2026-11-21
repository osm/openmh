#include <float.h>
#include <string.h>

#include "backends/render/software_material.h"
#include "backends/render/software_reflection.h"
#include "backends/render/software_renderer_internal.h"

#define MATRIX_W_MIN 1e-20f
#define MATRIX_W_MAX 1e20f
#define COLOR_BYTE_SCALE 255.0f

/*
 * Matrix-only inspection adapter. Explicit lens frames use the original
 * software point cache and near splitter instead.
 */
static int
project_matrix_vertex(const struct mh_software_surface *surface,
	const float matrix[16], const float *model,
	const struct mh_vertex *vertex, float xy[2], float *weight)
{
	float position[4], clip[4];
	unsigned int row;

	for (row = 0; row < 3; row++)
		position[row] = model == NULL
			? vertex->position[row]
			: model[row] * vertex->position[0] +
				model[4 + row] * vertex->position[1] +
				model[8 + row] * vertex->position[2] +
				model[12 + row];

	position[3] = 1;

	for (row = 0; row < 4; row++)
		clip[row] = matrix[row] * position[0] +
			matrix[4 + row] * position[1] +
			matrix[8 + row] * position[2] + matrix[12 + row];

	if (!(clip[3] >= MATRIX_W_MIN && clip[3] <= MATRIX_W_MAX &&
		clip[2] >= 0 && clip[2] <= clip[3]))
		return 0;

	/* Raw reciprocal camera Z, without near scaling. */
	*weight = (float)(1.0 / (double)clip[3]);
	xy[0] = (clip[0] / clip[3] + 1.0f) * (float)surface->width * 0.5f;
	xy[1] = (1.0f - clip[1] / clip[3]) * (float)surface->height * 0.5f;

	return 1;
}

static int
vertex_color(float color[3], int smooth, const struct mh_vertex *vertex,
	const struct mh_vertex *first)
{
	unsigned int channel;

	for (channel = 0; channel < 3; channel++) {
		if (!(vertex->color[channel] >= 0 &&
			vertex->color[channel] <= 1))
			return 0;

		if (!smooth && vertex->color[channel] != first->color[channel])
			return 0;

		color[channel] = vertex->color[channel] * COLOR_BYTE_SCALE;
	}

	return 1;
}

static void
world_vertex_color(const struct mh_mesh *mesh, size_t index, float color[3])
{
	const unsigned char *source;
	unsigned int channel;

	source = mesh->world_colors + index * 3;

	/* Store scene attributes in binary32 precision. No early clamp. */
	for (channel = 0; channel < 3; channel++)
		color[channel] = (float)((double)source[channel] *
			mesh->world_brightness[channel]);
}

/* One reusable shaded palette; source images remain borrowed and immutable. */
int
mh_software_triangle_bind_texture(struct mh_software_renderer *software,
	struct mh_software_draw_triangle *prepared)
{
	const struct mh_indexed_image *image;

	image = prepared->image;

	if (image == NULL)
		return 1;

	prepared->texture.indices = image->indices;
	prepared->texture.size = image->size;
	prepared->texture.width = image->width;
	prepared->texture.height = image->height;
	prepared->texture.palette = &software->palette;
	prepared->raster.texture = &prepared->texture;

	return mh_software_palette_build(&software->palette, image->palette,
		sizeof(image->palette), image->colors, &prepared->raster,
		software->surface.bits[1], NULL) == MH_ERROR_NONE;
}

/* Expanded mesh quads repeat corners 0 and 2 in their second triangle. */
static int
same_corner(const struct mh_vertex *a, const struct mh_vertex *b)
{
	unsigned int i;

	if (a->projection_w != b->projection_w)
		return 0;

	for (i = 0; i < 3; i++)
		if (a->position[i] != b->position[i] ||
			a->color[i] != b->color[i])
			return 0;

	return a->uv[0] == b->uv[0] && a->uv[1] == b->uv[1];
}

static int
project_face(struct mh_software_renderer *software,
	const struct mh_software_mesh *mesh,
	const struct mh_mesh_primitive *primitive,
	const struct mh_software_face_view *view,
	const struct mh_indexed_image *image, int reflect,
	struct mh_software_clipped_face *face,
	struct mh_software_clipped_face output[2], unsigned int *count,
	struct mh_software_point points[4])
{
	static const unsigned int corners[4] = {0, 1, 2, 5};
	const struct mh_vertex *vertices, *vertex;
	struct mh_software_clipped_corner *corner;
	struct mh_near_corner local[4];
	struct mh_software_point point;
	const struct mh_mesh_face *source_face;
	float lit_color[3];
	int lit, smooth, near_clipped;
	unsigned int i;

	vertices = view->object == NULL
		? &mesh->vertices[primitive->first]
		: &view->object->vertices[primitive->first -
			view->object->vertex_first];
	lit = !mesh->source->draws[primitive->draw].prelit;
	smooth = (face->flags & MH_NEAR_VERTEX_COLOR) != 0;
	near_clipped = 0;
	source_face = NULL;

	if (lit) {
		if (view->projection == NULL)
			return 0;

		source_face = view->object == NULL
			? &mesh->source
				->faces[primitive - mesh->source->primitives]
			: &view->object->faces[primitive -
				mesh->source->primitives -
				view->object->span.primitive_first];

		/* Original unavailable producers drop faces crossing near. */
		if (view->lighting->settings.mode == MH_LIGHT_UNAVAILABLE)
			for (i = 0; i < face->count; i++) {
				memset(&point, 0, sizeof(point));

				if (mh_software_project_point(view->projection,
					view->camera,
					vertices[corners[i]].position,
					&point, NULL) != MH_ERROR_NONE)
					return 0;

				if (point.outcode & MH_CLIP_NEAR) {
					*count = 0;
					return 1;
				}
			}

		/* Only reduced-detail triangles have a near-dependent shade. */
		if (!reflect && source_face->type == 8 &&
			view->lighting->settings.flat_detail)
			for (i = 0; i < 3; i++) {
				memset(&point, 0, sizeof(point));

				if (mh_software_project_point(view->projection,
					view->camera, vertices[i].position,
					&point, NULL) != MH_ERROR_NONE)
					return 0;

				near_clipped |=
					(point.outcode & MH_CLIP_NEAR) != 0;
			}

		if (!smooth &&
			!mh_software_model_light_color(view->lighting,
				source_face, vertices, view->camera,
				near_clipped, lit_color))
			return 0;
	}

	if (face->count == 4 &&
		(!same_corner(vertices, vertices + 3) ||
			!same_corner(vertices + 2, vertices + 4)))
		return 0;

	memset(local, 0, sizeof(local));

	for (i = 0; i < face->count; i++) {
		vertex = vertices + corners[i];
		corner = &face->corners[i];

		if (vertex->projection_w != 0)
			return 0;

		if (lit && smooth) {
			if (!mh_software_model_light_color(view->lighting,
				source_face, vertex, view->camera,
				near_clipped, corner->rgb))
				return 0;
		} else if (lit)
			memcpy(corner->rgb, lit_color, sizeof(lit_color));
		else if (mesh->source->world_colors != NULL &&
			primitive->first + corners[i] <
				mesh->source->world_vertex_count)
			world_vertex_color(mesh->source,
				primitive->first + corners[i], corner->rgb);
		else if (!vertex_color(corner->rgb,
				(face->flags & MH_NEAR_VERTEX_COLOR) != 0,
				vertex, vertices))
			return 0;

		if (reflect) {
			memset(&point, 0, sizeof(point));

			if (mh_software_project_point(view->projection,
				view->camera, vertex->position, &point,
				NULL) != MH_ERROR_NONE)
				return 0;

			/*
			 * Far geometry is culled by the face projection below.
			 */
			if (!(point.outcode & MH_CLIP_FAR) &&
				mh_software_reflection_uv(view->projection,
					view->camera, vertex->normal, &point,
					image->width, image->height, corner->uv,
					NULL) != MH_ERROR_NONE)
				return 0;
		} else if (image != NULL) {
			corner->uv[0] = vertex->uv[0] * image->width;
			corner->uv[1] = vertex->uv[1] * image->height;
		}

		if (view->projection != NULL) {
			memcpy(local[i].camera, vertex->position,
				sizeof(local[i].camera));
			memcpy(local[i].uv, corner->uv, sizeof(local[i].uv));
			memcpy(local[i].rgb, corner->rgb, sizeof(local[i].rgb));
		} else if (!project_matrix_vertex(&software->surface,
				view->matrix, view->model, vertex,
				corner->xy, &corner->weight))
			return 0;
	}

	if (view->projection != NULL)
		return mh_software_project_face(view->projection, view->camera,
			face->flags, local, output, count, points,
			NULL) == MH_ERROR_NONE;

	output[0] = *face;
	*count = 1;

	return 1;
}

int
mh_software_primitive_prepare(struct mh_software_renderer *software,
	const struct mh_software_mesh *mesh,
	const struct mh_mesh_primitive *primitive,
	const struct mh_software_face_view *view, size_t *cursor,
	unsigned int *bucket)
{
	struct mh_software_clipped_face face, faces[2];
	struct mh_software_material material;
	struct mh_software_texture texture;
	struct mh_software_triangle triangles[2];
	struct mh_software_point points[4];
	struct mh_software_draw_triangle *prepared;
	const struct mh_indexed_image *image;
	const struct mh_draw *draw;
	unsigned int type, triangle_count, triangle, face_index, face_count;
	int reflect, unavailable;

	memset(&face, 0, sizeof(face));
	memset(&material, 0, sizeof(material));
	memset(&texture, 0, sizeof(texture));
	unavailable = view->lighting->enabled &&
		view->lighting->settings.mode == MH_LIGHT_UNAVAILABLE;
	type = primitive->type & ~1U;
	reflect = type == 8 &&
		mh_software_model_reflects(view->lighting, primitive->flags);
	face.count = primitive->type & 1U ? 4U : 3U;
	face.flags = face.count == 4 ? MH_NEAR_QUAD : 0;

	if (!unavailable &&
		(type == 4 ||
			(type == 8 && !reflect &&
				!view->lighting->settings.flat_detail)))
		face.flags |= MH_NEAR_VERTEX_COLOR;

	image = NULL;
	draw = &mesh->source->draws[primitive->draw];
	material.mapping = software->mapping;
	material.perspective_shift = software->perspective_shift;

	if (unavailable)
		face.flags |= MH_SOFTWARE_FACE_OUTLINE;
	else if (reflect) {
		if (!software->environment.loaded)
			return 0;

		face.flags |= MH_NEAR_TEXTURED;
		image = &software->environment.maps
				[((primitive->flags &
					MH_SOFTWARE_MATERIAL_ENVIRONMENT_MASK) >>
					5) -
					1];
		material.texture = &texture;
	} else if (!unavailable && type == 2) {
		face.flags |= MH_NEAR_TEXTURED;
		image = &mesh->source->indexed_textures[draw->texture];
		material.texture = &texture;
		texture.zero_transparent =
			(primitive->flags & MH_SOFTWARE_MATERIAL_CUTOUT) != 0 ||
			draw->alpha_test;

		if (texture.zero_transparent)
			face.flags |= MH_SOFTWARE_FACE_CUTOUT;

		if (primitive->flags & MH_SOFTWARE_MATERIAL_PERSPECTIVE)
			face.flags |= MH_SOFTWARE_FACE_PERSPECTIVE;
	}

	if (!project_face(software, mesh, primitive, view, image, reflect,
		&face, faces, &face_count, points))
		return 0;

	*bucket = 0;

	if (face_count != 0 && view->order != NULL &&
		mh_software_order_bucket(view->order, points, face.count,
			view->producer_mode, primitive->flags, bucket,
			NULL) != MH_ERROR_NONE)
		return 0;

	if (face_count != 0 && view->fog != NULL) {
		if (mh_software_fog_amount(view->fog, points, face.count,
			&material.fog, NULL) != MH_ERROR_NONE)
			return 0;

		memcpy(material.fog_color, view->cue->color,
			sizeof(material.fog_color));

		if (material.fog > 0 && view->cue->gradient != NULL &&
			mh_software_fog_color(view->cue->gradient, points,
				face.count, material.fog_color,
				NULL) != MH_ERROR_NONE)
			return 0;
	}

	for (face_index = 0; face_index < face_count; face_index++) {
		if (mh_software_face_prepare(&faces[face_index], &material,
			triangles, &triangle_count,
			NULL) != MH_ERROR_NONE ||
			triangle_count > software->triangle_capacity - *cursor)
			return 0;

		for (triangle = 0; triangle < triangle_count; triangle++) {
			prepared = &software->triangles[(*cursor)++];
			memset(prepared, 0, sizeof(*prepared));
			prepared->raster = triangles[triangle];
			prepared->image = image;
			prepared->texture.zero_transparent =
				texture.zero_transparent;
		}
	}

	return 1;
}

/* Reject a triangle before touching raster step history. */
static int
outside_texture_image(const struct mh_software_draw_triangle *triangle)
{
	unsigned int i;
	const float *uv;

	if (triangle->image == NULL)
		return 0;

	/* Non-finite native inputs still fail validation atomically. */
	for (i = 0; i < 3; i++) {
		uv = triangle->raster.uv[i];

		if (!(uv[0] >= -FLT_MAX && uv[0] <= FLT_MAX &&
			uv[1] >= -FLT_MAX && uv[1] <= FLT_MAX))
			return 0;
	}

	for (i = 0; i < 3; i++) {
		uv = triangle->raster.uv[i];

		if (uv[0] < 0 || uv[1] < 0 ||
			uv[0] > (float)triangle->image->width - .5f ||
			uv[1] > (float)triangle->image->height - .5f)
			return 1;
	}

	return 0;
}

int
mh_software_triangle_validate(struct mh_software_renderer *software,
	struct mh_software_draw_triangle *triangle,
	struct mh_software_texture_state *state, int lens)
{
	triangle->rejected = lens && outside_texture_image(triangle);

	if (triangle->rejected)
		return 1;

	return mh_software_triangle_bind_texture(software, triangle) &&
		mh_software_triangle_check(&software->surface,
			&triangle->raster, state, NULL) == MH_ERROR_NONE;
}
