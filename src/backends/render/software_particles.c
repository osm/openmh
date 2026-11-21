#include <stdlib.h>
#include <string.h>

#include "backends/render/software_order.h"
#include "backends/render/software_particles.h"

#define PARTICLE_LIMIT 1e6f
#define IMAGE_LIMIT 256
#define POOL_VISIBILITY_RADIUS 20
#define PARTICLE_VISIBILITY_RADIUS 2
#define BRIGHTNESS_LIMIT 200
#define GROWTH_BASE 1.5
#define GROWTH_REMAINING 0.5
#define FADE_BASE 40.0
#define FADE_GROWTH 17.78
#define FADE_SCALE 3.0
#define STEADY_LIGHT 120.0
#define SPARK_SHORT_SIDE 0.2
#define SPARK_LONG_SIDE 2.0

static enum mh_error_code
particle_bucket(const struct mh_projection *projection, float depth,
	unsigned int *bucket, struct mh_error *error_out)
{
	struct mh_software_order order;
	const struct mh_software_depth_band *band;
	float position;
	enum mh_error_code status;

	status = mh_software_order_setup(projection->clip.near_distance,
		projection->far_distance, MH_SOFTWARE_ORDER_ROOT, &order,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Raw center depth, plain non-mode-1 band. */
	band = &order.bands[0][MH_SOFTWARE_ORDER_PLAIN];
	position = (float)((double)depth - order.near_distance);
	position = (float)((double)position * order.inverse_range);
	position = (float)(1.0 - position);
	position = (float)((double)position * band->scale);
	position = (float)((double)position + band->offset);
	position = (float)((double)position * (order.buckets - 1));

	if (!(position > -1 && position < order.buckets))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software particle outside ordering buckets");

	*bucket = (unsigned int)(long)position;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_particle_quad(const struct mh_particle *particle,
	const struct mh_projection *projection,
	const struct mh_software_point *center, float brightness,
	const struct mh_indexed_image *image,
	struct mh_software_particle_face *output, struct mh_error *error_out)
{
	struct mh_software_particle_face next;
	struct mh_software_clipped_corner *vertex;
	float growth, light, factor, width, height, left, right, top, bottom;
	float color[3], u0, u1;
	unsigned int channel, corner;
	enum mh_error_code status;

	if (particle == NULL || center == NULL || image == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software particle inputs");

	status = mh_software_projection_check(projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (center->outcode != 0 ||
		!(center->value[2] > projection->clip.near_distance &&
			center->value[2] < projection->far_distance) ||
		!(center->value[3] > 0 && center->value[3] <= PARTICLE_LIMIT) ||
		!(particle->lifespan > 0 &&
			particle->lifespan <= PARTICLE_LIMIT) ||
		!(particle->remaining >= 0 &&
			particle->remaining <= particle->lifespan) ||
		!(particle->width >= 0 && particle->width <= PARTICLE_LIMIT) ||
		!(particle->height >= 0 &&
			particle->height <= PARTICLE_LIMIT) ||
		!(brightness >= 0 && brightness <= BRIGHTNESS_LIMIT) ||
		(particle->flags != 3 && particle->flags != 0x43) ||
		particle->style > 7 || image->width == 0 ||
		image->width > IMAGE_LIMIT || image->height == 0 ||
		image->height > IMAGE_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software particle geometry");

	growth = (float)((double)particle->remaining / particle->lifespan);
	growth = (float)((long double)growth * GROWTH_REMAINING);
	growth = (float)((long double)GROWTH_BASE - growth);
	growth = (float)((double)growth * growth);
	light = STEADY_LIGHT;

	if (particle->flags & MH_PARTICLE_FADE) {
		light = (float)((long double)growth * FADE_GROWTH);
		light = (float)((long double)FADE_BASE - light);
		light = (float)((long double)light * FADE_SCALE);
		factor = (float)((double)center->value[2] *
			projection->inverse_far);
		factor = (float)(1.0 - factor);
		light = (float)((double)light * factor);
	}

	for (channel = 0; channel < 3; channel++) {
		if (!(particle->color[channel] >= 0 &&
			particle->color[channel] <= BRIGHTNESS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"invalid software particle color");

		color[channel] =
			(float)((double)light * particle->color[channel]);
		color[channel] = (float)((double)color[channel] * brightness);
	}

	factor = (float)((double)center->value[3] * projection->clip.focal);
	width = (float)((double)growth * particle->width);
	width = (float)((double)width * factor);
	height = (float)((double)growth * particle->height);
	height = (float)((double)height * factor);

	if (width > .5f)
		width = .5f;

	if (height > .5f)
		height = .5f;

	width = (float)((double)width * projection->clip.half_width);
	factor = (float)((double)projection->clip.half_width *
		projection->clip.aspect);
	height = (float)((double)factor * height);
	left = right = width;
	u0 = .5f;
	u1 = image->width - .5f;

	if (particle->style & MH_PARTICLE_STYLE_SPARK) {
		left = (float)((long double)width * SPARK_LONG_SIDE);
		right = (float)((long double)width * SPARK_SHORT_SIDE);
		u0 = u1;
		u1 = .5f;

		if (particle->style & MH_PARTICLE_STYLE_RIGHT) {
			left = right;
			right = (float)((long double)width * SPARK_LONG_SIDE);
			u1 = u0;
			u0 = .5f;
		}
	}

	left = (float)((double)center->value[0] - left);
	right = (float)((double)center->value[0] + right);
	top = (float)((double)center->value[1] - height);
	bottom = (float)((double)center->value[1] + height);
	memset(&next, 0, sizeof(next));
	next.image = image;
	next.face.flags = particle->flags;
	next.face.count = 4;

	for (corner = 0; corner < 4; corner++) {
		vertex = &next.face.corners[corner];
		vertex->xy[0] = corner == 0 || corner == 3 ? left : right;
		vertex->xy[1] = corner < 2 ? top : bottom;
		vertex->uv[0] = corner == 0 || corner == 3 ? u0 : u1;
		vertex->uv[1] = corner < 2 ? .5f : image->height - .5f;
		memcpy(vertex->rgb, color, sizeof(color));
	}

	status = particle_bucket(
		projection, center->value[2], &next.bucket, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_software_particles_free(struct mh_software_particles *particles)
{
	if (particles == NULL)
		return;

	free(particles->faces);
	memset(particles, 0, sizeof(*particles));
}

enum mh_error_code
mh_software_particles_prepare(struct mh_software_particles *output,
	const struct mh_particles *states, unsigned int cars,
	const struct mh_software_particle_assets *assets,
	const struct mh_projection *projection, const float camera[12],
	const struct mh_frustum *frustum, float brightness, int smoke,
	int sparks, struct mh_error *error_out)
{
	struct mh_software_particle_face *faces;
	struct mh_software_point center;
	const struct mh_particle_pool *pool;
	const struct mh_particle *particle;
	const struct mh_software_particle_animation *animation;
	size_t capacity, face_count;
	unsigned int car, group, slot, newest;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software particle scratch");

	output->count = 0;

	if (states == NULL || assets == NULL || camera == NULL ||
		frustum == NULL || cars > MH_SOFTWARE_PARTICLE_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software particle submission");

	capacity = (size_t)cars * MH_PARTICLE_POOLS * MH_PARTICLE_CAPACITY;

	if (capacity > output->capacity) {
		faces = realloc(output->faces, capacity * sizeof(*faces));

		if (faces == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory preparing software particles");

		output->faces = faces;
		output->capacity = capacity;
	}

	face_count = 0;

	for (car = 0; car < cars; car++)
		for (group = 0; group < MH_PARTICLE_POOLS; group++) {
			pool = &states[car].pools[group];

			if (!pool->enabled || pool->count == 0)
				continue;

			if (pool->count > MH_PARTICLE_CAPACITY ||
				pool->first >= MH_PARTICLE_CAPACITY)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software particle pool");

			newest = (pool->first + pool->count - 1) %
				MH_PARTICLE_CAPACITY;

			if (!mh_frustum_sphere_profile(MH_RENDER_SOFTWARE,
				frustum, pool->particles[newest].position,
				POOL_VISIBILITY_RADIUS))
				continue;

			for (slot = 0; slot < MH_PARTICLE_CAPACITY; slot++) {
				particle = &pool->particles[slot];

				if (!particle->active ||
					!(particle->style & MH_PARTICLE_STYLE_SPARK
							? sparks
							: smoke) ||
					!mh_frustum_sphere_profile(
						MH_RENDER_SOFTWARE, frustum,
						particle->position,
						PARTICLE_VISIBILITY_RADIUS))
					continue;

				memset(&center, 0, sizeof(center));
				status = mh_software_project_point(projection,
					camera, particle->position, &center,
					error_out);

				if (status != MH_ERROR_NONE)
					return status;

				if (center.outcode != 0)
					continue;

				if (particle->animation < 1 ||
					particle->animation >
						MH_PARTICLE_ANIMATIONS)
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"invalid software particle animation");

				animation = &assets->animations
						[particle->animation - 1];

				if (animation->count >
						MH_SOFTWARE_PARTICLE_FRAMES ||
					particle->frame >= animation->count)
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"invalid software particle frame");

				status = mh_software_particle_quad(particle,
					projection, &center, brightness,
					&animation->frames[particle->frame],
					&output->faces[face_count], error_out);

				if (status != MH_ERROR_NONE)
					return status;

				face_count++;
			}
		}

	output->count = face_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
