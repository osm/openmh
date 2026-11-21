#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/render/effect_particles.h"

#define SMOKE_FRAMES 9U
#define SPARK_FRAMES 6U

#define GRASS_FIRST_FRAME 9
#define SPARK_FIRST_FRAME 18
#define SMOKE_FRAME_SIZE 32
#define SPARK_FRAME_SIZE 64
#define ATLAS_WRAP_MASK 255U
#define PARTICLE_RADIUS 20
#define QUAD_VERTICES 6
#define VIEWPORT_LIMIT 20000
#define PARTICLE_VERTICES                                                      \
	(MH_PARTICLE_POOLS * MH_PARTICLE_CAPACITY * QUAD_VERTICES)

static int
within_limit(float value, float limit)
{
	return value >= -limit && value <= limit;
}

enum mh_error_code
mh_particle_quad(const struct mh_particle *particle,
	const struct mh_particle_projection *projection, const float uv[8],
	struct mh_particle_quad *output, struct mh_error *error_out)
{
	struct mh_particle_quad result;
	long double age_scale, biased;
	float growth, brightness, factor, width, height, left, right, depth;
	unsigned int i, source;

	if (particle == NULL || projection == NULL || uv == NULL ||
		output == NULL || particle->flags > 255 ||
		particle->style > 7 ||
		!(particle->lifespan > 0 && particle->lifespan <= 10) ||
		!(particle->remaining >= 0 &&
			particle->remaining <= particle->lifespan) ||
		!(particle->width >= 0 && particle->width <= 1e30f) ||
		!(particle->height >= 0 && particle->height <= 1e30f) ||
		!(projection->near_depth > 0 &&
			projection->near_depth <= 10000) ||
		!(projection->far_depth >= projection->near_depth &&
			projection->far_depth <= 1e6) ||
		!(projection->focal > 0 && projection->focal <= 100) ||
		!(projection->half_width > 0 &&
			projection->half_width <= 10000) ||
		!(projection->pixel_aspect > 0 &&
			projection->pixel_aspect <= 100) ||
		!(projection->inverse_far >= 0 &&
			projection->inverse_far <= 10000) ||
		!(projection->brightness >= 0 &&
			projection->brightness <= 1000) ||
		!within_limit(projection->center[0], 1e6f) ||
		!within_limit(projection->center[1], 1e6f) ||
		!(projection->center[2] >= 0 && projection->center[2] <= 1) ||
		!(projection->center[3] > 0 && projection->center[3] <= 1e6))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid particle projection");

	for (i = 0; i < 3; i++)
		if (!(particle->color[i] >= 0 && particle->color[i] <= 255))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid particle color");

	for (i = 0; i < 8; i++)
		if (!within_limit(uv[i], 1e6f))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid particle UV");

	age_scale = 1.5L -
		(long double)particle->remaining / particle->lifespan * .5L;
	growth = (float)(age_scale * (float)age_scale);

	if (particle->flags & MH_PARTICLE_FADE) {
		/* Store only after the depth fade. */
		brightness = (float)((40 -
					(long double)growth *
						(long double)17.78) *
			3 *
			(1 -
				(long double)projection->center[2] *
					projection->inverse_far));

		if (particle->style & MH_PARTICLE_STYLE_SMOKE)
			brightness = (float)((long double)brightness * .75L);
	} else
		brightness = 120;

	for (i = 0; i < 3; i++) {
		result.color[i] = (float)((long double)brightness *
			particle->color[i] * projection->brightness);

		if (result.color[i] < 0)
			result.color[i] = 0;

		if (result.color[i] > 255)
			result.color[i] = 255;
	}

	factor =
		(float)((long double)projection->center[3] * projection->focal);

	if (!(particle->style & MH_PARTICLE_STYLE_SMOKE) &&
		(particle->style & MH_PARTICLE_STYLE_SPARK))
		factor = (float)((long double)factor * .75L);

	width = (float)((long double)growth * particle->width * factor);
	height = (float)((long double)growth * particle->height * factor);

	if (width > .5f)
		width = .5f;

	if (height > .5f)
		height = .5f;

	width = (float)((long double)width * projection->half_width);
	height = (float)((long double)projection->half_width *
		projection->pixel_aspect * height);
	biased = (long double)projection->center[2] * projection->far_depth -
		((particle->style & MH_PARTICLE_STYLE_SPARK) ? 2 : 1);
	depth = (float)biased;
	depth = biased > projection->near_depth
		? (float)((long double)projection->near_depth / depth)
		: .999989986419677734375f;

	left = (float)((long double)projection->center[0] - width);
	right = (float)((long double)projection->center[0] + width);

	if (particle->style & MH_PARTICLE_STYLE_SPARK) {
		if (particle->style & MH_PARTICLE_STYLE_RIGHT) {
			left = projection->center[0];
			right = (float)((long double)projection->center[0] +
				(long double)width * 2);
		} else {
			left = (float)((long double)projection->center[0] -
				(long double)width * 2);
			right = projection->center[0];
		}
	}

	for (i = 0; i < 4; i++) {
		result.position[i][0] = i == 0 || i == 3 ? left : right;
		result.position[i][1] =
			(float)((long double)projection->center[1] +
				(i < 2 ? -(long double)height : height));
		result.position[i][2] = depth;
		source = (particle->style & MH_PARTICLE_STYLE_SPARK) &&
				!(particle->style & MH_PARTICLE_STYLE_RIGHT)
			? (i ^ 1U)
			: i;

		result.uv[i][0] = uv[source * 2];
		result.uv[i][1] = uv[source * 2 + 1];
	}

	result.reciprocal_depth = projection->center[3];
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_particle_frame_uv(unsigned int animation, unsigned int frame, float uv[8],
	struct mh_error *error_out)
{
	unsigned int index, i, x, y, bottom, size;
	float left, top, right, lower;

	if (uv == NULL || animation < MH_PARTICLE_SMOKE ||
		animation > MH_PARTICLE_SPARK ||
		frame >= (animation == MH_PARTICLE_SPARK ? SPARK_FRAMES
							: SMOKE_FRAMES))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid hardware particle frame");

	if (animation == MH_PARTICLE_SPARK)
		index = frame + SPARK_FIRST_FRAME;
	else if (animation == MH_PARTICLE_SMOKE)
		index = frame;
	else
		index = frame + GRASS_FIRST_FRAME;

	x = y = bottom = 0;
	size = SMOKE_FRAME_SIZE;

	for (i = 0; i <= index; i++) {
		size = i < SPARK_FIRST_FRAME ? SMOKE_FRAME_SIZE
					: SPARK_FRAME_SIZE;

		/*
		 * Update the row bottom only at byte-X wrap. This preserves the
		 * overlapping 32-to-64px transition.
		 */
		if (x == 0) {
			y = bottom;
			bottom = (bottom + size) & ATLAS_WRAP_MASK;
		}

		if (MH_PARTICLE_ATLAS_SIZE - x < size) {
			x = 0;
			y = bottom;
		}

		if (i != index)
			x = (x + size) & ATLAS_WRAP_MASK;
	}

	left = (float)(x + 1) / MH_PARTICLE_ATLAS_SIZE;
	top = (float)(y + 1) / MH_PARTICLE_ATLAS_SIZE;
	right = (float)(x + size - 1) / MH_PARTICLE_ATLAS_SIZE;
	lower = (float)(y + size - 1) / MH_PARTICLE_ATLAS_SIZE;
	uv[0] = uv[6] = left;
	uv[2] = uv[4] = right;
	uv[1] = uv[3] = top;
	uv[5] = uv[7] = lower;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_particle_mesh_create(
	unsigned int cars, struct mh_mesh *output, struct mh_error *error_out)
{
	struct mh_mesh mesh;
	size_t pixel;

	if (cars < 1 || cars > MH_RACE_MAX_CARS || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid particle mesh size");

	memset(&mesh, 0, sizeof(mesh));
	mesh.vertex_count = cars * PARTICLE_VERTICES;
	mesh.draw_count = 1;
	mesh.vertices = (struct mh_vertex *)calloc(
		mesh.vertex_count, sizeof(*mesh.vertices));
	mesh.draws = (struct mh_draw *)calloc(1, sizeof(*mesh.draws));
	mesh.textures = (struct mh_image *)calloc(1, sizeof(*mesh.textures));

	if (mesh.textures != NULL)
		mesh.texture_count = 1;

	if (mesh.vertices == NULL || mesh.draws == NULL ||
		mesh.textures == NULL) {
		mh_mesh_free(&mesh);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory for particle mesh");
	}

	mesh.textures[0].rgba =
		(unsigned char *)calloc((unsigned long)MH_PARTICLE_ATLAS_SIZE *
				MH_PARTICLE_ATLAS_SIZE * 4,
			1);

	if (mesh.textures[0].rgba == NULL) {
		mh_mesh_free(&mesh);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory for particle atlas");
	}

	mesh.textures[0].width = mesh.textures[0].height =
		MH_PARTICLE_ATLAS_SIZE;

	for (pixel = 0; pixel <
		(unsigned long)MH_PARTICLE_ATLAS_SIZE * MH_PARTICLE_ATLAS_SIZE;
		pixel++)
		mesh.textures[0].rgba[pixel * 4 + 3] = 255;

	mesh.draws[0].count = mesh.vertex_count;
	mesh.draws[0].prelit = 1;
	*output = mesh;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
sphere_visible(const float position[3], const float matrix[16])
{
	double plane[4], norm, dot;
	unsigned int j, side, row;
	int sign;

	for (side = 0; side < 6; side++) {
		row = side / 2;
		sign = side % 2 ? -1 : 1;

		for (j = 0; j < 4; j++)
			plane[j] = (side == 4 ? 0 : matrix[j * 4 + 3]) +
				sign * (double)matrix[j * 4 + row];

		norm = sqrt(plane[0] * plane[0] + plane[1] * plane[1] +
			plane[2] * plane[2]);
		dot = plane[3];

		for (j = 0; j < 3; j++)
			dot += plane[j] * position[j];

		if (dot < -PARTICLE_RADIUS * norm)
			return 0;
	}

	return 1;
}

static void
transform_position(
	const float position[3], const float matrix[16], double clip[4])
{
	unsigned int row, axis;

	for (row = 0; row < 4; row++) {
		clip[row] = matrix[12 + row];

		for (axis = 0; axis < 3; axis++)
			clip[row] +=
				(double)matrix[axis * 4 + row] * position[axis];
	}
}

static void
append_quad(const struct mh_particle_quad *quad,
	const struct mh_particle_camera *camera,
	struct mh_vertex vertices[QUAD_VERTICES], float *raw_rgb)
{
	static const unsigned int triangles[QUAD_VERTICES] = {0, 1, 2, 0, 2, 3};
	struct mh_vertex *vertex;
	double depth;
	unsigned int index, channel, corner;

	depth = (1 - (double)quad->position[0][2]) /
		(1 - (double)camera->near_depth / camera->far_depth);

	if (depth < 0)
		depth = 0;

	if (depth > 1)
		depth = 1;

	for (index = 0; index < QUAD_VERTICES; index++) {
		vertex = &vertices[index];
		corner = triangles[index];
		vertex->position[0] =
			(float)((double)quad->position[corner][0] * 2 /
					camera->width -
				1);
		vertex->position[1] = (float)(1 -
			(double)quad->position[corner][1] * 2 / camera->height);
		vertex->position[2] = (float)depth;

		if (raw_rgb != NULL) {
			vertex->projection_w = 1 / quad->reciprocal_depth;
			memcpy(raw_rgb + index * 3, quad->color,
				sizeof(quad->color));
		}

		memcpy(vertex->uv, quad->uv[corner], sizeof(vertex->uv));

		for (channel = 0; channel < 3; channel++)
			vertex->color[channel] =
				(float)(unsigned int)quad->color[channel] / 255;
	}
}

enum mh_error_code
mh_particle_mesh_update(struct mh_mesh *mesh, const struct mh_particles *states,
	unsigned int cars, const struct mh_particle_camera *camera,
	struct mh_error *error_out)
{
	struct mh_vertex vertices[MH_RACE_MAX_CARS * PARTICLE_VERTICES];
	struct mh_particle_projection projection;
	struct mh_particle_quad quad;
	const struct mh_particle_pool *pool;
	const struct mh_particle *particle;
	float uv[8], raw_rgb[MH_RACE_MAX_CARS * PARTICLE_VERTICES * 3];
	double clip[4], focal, vertical;
	unsigned int car, pool_index, slot, i, latest;
	size_t used;
	enum mh_error_code status;

	if (mesh == NULL || states == NULL || camera == NULL || cars < 1 ||
		cars > MH_RACE_MAX_CARS || mesh->vertices == NULL ||
		mesh->vertex_count != cars * PARTICLE_VERTICES ||
		(mesh->material_rgb != NULL &&
			mesh->material_rgb_count != mesh->vertex_count) ||
		camera->width < 1 || camera->width > VIEWPORT_LIMIT ||
		camera->height < 1 || camera->height > VIEWPORT_LIMIT ||
		!(camera->near_depth > 0 && camera->near_depth <= 10000) ||
		!(camera->far_depth > camera->near_depth &&
			camera->far_depth <= 1e6) ||
		!(camera->brightness >= 0 && camera->brightness <= 1000) ||
		(camera->smoke != 0 && camera->smoke != 1) ||
		(camera->sparks != 0 && camera->sparks != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid particle mesh camera");

	for (i = 0; i < 16; i++)
		if (!within_limit(camera->matrix[i], 1e7f))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid particle matrix");

	focal = sqrt((double)camera->matrix[0] * camera->matrix[0] +
		(double)camera->matrix[4] * camera->matrix[4] +
		(double)camera->matrix[8] * camera->matrix[8]);

	if (!(focal > 0 && focal <= 100))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid particle focal scale");

	vertical = sqrt((double)camera->matrix[1] * camera->matrix[1] +
		(double)camera->matrix[5] * camera->matrix[5] +
		(double)camera->matrix[9] * camera->matrix[9]);

	if (!(vertical > 0 && vertical <= 100))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid particle vertical scale");

	projection.near_depth = camera->near_depth;
	projection.far_depth = camera->far_depth;
	projection.focal = (float)focal;
	projection.half_width = (float)camera->width * .5f;
	projection.pixel_aspect =
		(float)(vertical / focal * camera->height / camera->width);
	projection.inverse_far = (float)(1.0L / camera->far_depth);
	projection.brightness = camera->brightness;
	memset(vertices, 0, mesh->vertex_count * sizeof(*vertices));
	used = 0;

	if (mesh->material_rgb != NULL)
		memset(raw_rgb, 0, mesh->vertex_count * 3 * sizeof(float));

	/*
	 * Original descriptors prepend to the batch list: submit reverse order.
	 */
	for (car = cars; car-- > 0;)
		for (pool_index = MH_PARTICLE_POOLS; pool_index-- > 0;) {
			pool = &states[car].pools[pool_index];

			if (pool->enabled > 1 ||
				pool->count > MH_PARTICLE_CAPACITY ||
				pool->first >= MH_PARTICLE_CAPACITY)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid particle pool indices");

			if (!pool->enabled || pool->count == 0)
				continue;

			latest = (pool->first + pool->count - 1) %
				MH_PARTICLE_CAPACITY;

			if (!sphere_visible(pool->particles[latest].position,
				camera->matrix))
				continue;

			for (slot = MH_PARTICLE_CAPACITY; slot-- > 0;) {
				particle = &pool->particles[slot];

				if (!particle->active ||
					((particle->style &
						MH_PARTICLE_STYLE_SPARK)
							? !camera->sparks
							: !camera->smoke))
					continue;

				for (i = 0; i < 3; i++)
					if (!within_limit(particle->position[i],
						1e30f))
						return mh_error_set(error_out,
							MH_ERROR_INVALID,
							"invalid particle position");

				transform_position(particle->position,
					camera->matrix, clip);

				if (clip[3] <= 0 ||
					clip[3] > camera->far_depth ||
					clip[2] < 0 || clip[2] > clip[3] ||
					clip[0] < -clip[3] ||
					clip[0] > clip[3] ||
					clip[1] < -clip[3] || clip[1] > clip[3])
					continue;

				projection.center[0] =
					(float)((clip[0] / clip[3] + 1) *
						camera->width * .5);
				projection.center[1] =
					(float)((1 - clip[1] / clip[3]) *
						camera->height * .5);
				projection.center[2] =
					(float)(clip[3] / camera->far_depth);
				projection.center[3] = (float)(1 / clip[3]);
				status = mh_particle_frame_uv(
					particle->animation, particle->frame,
					uv, error_out);

				if (status == MH_ERROR_NONE)
					status = mh_particle_quad(particle,
						&projection, uv, &quad,
						error_out);

				if (status != MH_ERROR_NONE)
					return status;

				append_quad(&quad, camera, vertices + used,
					mesh->material_rgb == NULL
						? NULL
						: raw_rgb + used * 3);
				used += QUAD_VERTICES;
			}
		}

	memcpy(mesh->vertices, vertices,
		mesh->vertex_count * sizeof(*vertices));

	if (mesh->material_rgb != NULL)
		memcpy(mesh->material_rgb, raw_rgb,
			mesh->vertex_count * 3 * sizeof(float));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
