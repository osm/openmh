#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/simulation/particles.h"

#define INPUT_LIMIT 1000000
#define BASIS_LIMIT 2
#define GROWTH_LIMIT 1e30f
#define TIME_LIMIT 10
#define INITIAL_FLAGS 0x43
#define RANDOM_MIDPOINT (MH_RANDOM_RESULT_COUNT / 2U)
#define HARDWARE_TYRE_FRAMES 9
#define SOFTWARE_TYRE_FRAMES 17
#define HARDWARE_SPARK_FRAMES 6
#define SOFTWARE_SPARK_FRAMES 20
#define FRAME_RATE 30
#define SOFTWARE_SPARK_FRAME_RATE 60
#define ANIMATION_FRAME_LIMIT 256
#define MIN_FRAME_SECONDS .001f
#define SPARK_GAIN_MIN .5f
#define SPARK_POOL_INDEX 2

enum particle_phase {
	PHASE_DONE = 0,
	PHASE_NEW = 1,
	PHASE_HELD = 2
};

static int
bounded(float value, float limit)
{
	return value >= -limit && value <= limit;
}

static int
valid_system(const struct mh_particle_system *system)
{
	const struct mh_particle_animation *animation;
	unsigned int animation_index;

	if (system == NULL || system->random > MH_U32_MAX ||
		(system->hardware != 0 && system->hardware != 1))
		return 0;

	for (animation_index = 0; animation_index < MH_PARTICLE_ANIMATIONS;
		animation_index++) {
		animation = &system->animations[animation_index];

		if (animation->flags > 7 ||
			animation->mode > MH_PARTICLE_FRAME_KEEP ||
			animation->frames < 1 ||
			animation->frames > ANIMATION_FRAME_LIMIT ||
			animation->cursor >= animation->frames ||
			!(animation->frame_seconds >= MIN_FRAME_SECONDS &&
				animation->frame_seconds <= TIME_LIMIT))
			return 0;
	}

	return 1;
}

static int
valid_particle(const struct mh_particle *particle,
	const struct mh_particle_system *system)
{
	const struct mh_particle_animation *animation;
	unsigned int axis;

	if (particle->animation < 1 ||
		particle->animation > MH_PARTICLE_ANIMATIONS)
		return 0;

	animation = &system->animations[particle->animation - 1];

	if (particle->active > 1 || particle->phase > PHASE_HELD ||
		particle->flags > 255 || particle->style > 7 ||
		particle->frame >= animation->frames ||
		!(particle->lifespan > 0 && particle->lifespan <= TIME_LIMIT) ||
		!(particle->remaining >= 0 &&
			particle->remaining <= particle->lifespan) ||
		!(particle->width >= 0 && particle->width <= GROWTH_LIMIT) ||
		!(particle->height >= 0 && particle->height <= GROWTH_LIMIT))
		return 0;

	for (axis = 0; axis < 3; axis++)
		if (!bounded(particle->position[axis], GROWTH_LIMIT) ||
			!(particle->color[axis] >= 0 &&
				particle->color[axis] <= 255))
			return 0;

	return 1;
}

static int
valid_particles(const struct mh_particles *particles,
	const struct mh_particle_system *system)
{
	const struct mh_particle_pool *pool;
	const struct mh_particle *particle;
	unsigned int pool_index, offset;

	if (particles == NULL)
		return 0;

	for (pool_index = 0; pool_index < MH_PARTICLE_POOLS; pool_index++) {
		pool = &particles->pools[pool_index];

		if (pool->enabled > 1 || pool->count > MH_PARTICLE_CAPACITY ||
			pool->first >= MH_PARTICLE_CAPACITY)
			return 0;

		for (offset = 0; offset < pool->count; offset++) {
			particle = &pool->particles[(pool->first + offset) %
				MH_PARTICLE_CAPACITY];

			if (!valid_particle(particle, system))
				return 0;
		}
	}

	return 1;
}

static int
valid_input(const struct mh_particle_input *input)
{
	unsigned int component, axis, channel, corner;

	if (input == NULL ||
		(input->force_smoke != 0 && input->force_smoke != 1))
		return 0;

	for (component = 0; component < 9; component++)
		if (!bounded(input->rotation[component], BASIS_LIMIT))
			return 0;

	for (axis = 0; axis < 3; axis++)
		if (!bounded(input->position[axis], INPUT_LIMIT))
			return 0;

	for (channel = 0; channel < 2; channel++) {
		if (input->spark_corner[channel] >= MH_CAR_BODY_POINTS ||
			!bounded(input->spark_gain[channel], INPUT_LIMIT))
			return 0;

		for (axis = 0; axis < 3; axis++)
			if (!bounded(input->wheels[channel][axis], INPUT_LIMIT))
				return 0;
	}

	for (corner = 0; corner < MH_CAR_BODY_POINTS; corner++)
		for (axis = 0; axis < 3; axis++)
			if (!bounded(input->corners[corner][axis], INPUT_LIMIT))
				return 0;

	return 1;
}

enum mh_error_code
mh_particle_system_init(struct mh_particle_system *system, int hardware,
	unsigned long seed, struct mh_error *error_out)
{
	struct mh_particle_system result;
	unsigned int animation_index;

	if (system == NULL || (hardware != 0 && hardware != 1) ||
		seed > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid particle system configuration");

	memset(&result, 0, sizeof(result));
	result.hardware = hardware;
	result.random = seed;

	for (animation_index = 0; animation_index < MH_PARTICLE_ANIMATIONS;
		animation_index++) {
		result.animations[animation_index].frames =
			hardware ? HARDWARE_TYRE_FRAMES : SOFTWARE_TYRE_FRAMES;
		result.animations[animation_index].frame_seconds =
			(float)(1.0L / FRAME_RATE);
	}

	result.animations[MH_PARTICLE_SMOKE - 1].flags = MH_PARTICLE_ANIM_GROW;
	result.animations[MH_PARTICLE_GRAVEL - 1].flags =
		hardware ? 0 : MH_PARTICLE_ANIM_GROW;
	result.animations[MH_PARTICLE_SPARK - 1].flags = MH_PARTICLE_ANIM_SPARK;
	result.animations[MH_PARTICLE_SPARK - 1].frames =
		hardware ? HARDWARE_SPARK_FRAMES : SOFTWARE_SPARK_FRAMES;
	result.animations[MH_PARTICLE_SPARK - 1].frame_seconds = (float)(1.0L /
		(hardware ? FRAME_RATE : SOFTWARE_SPARK_FRAME_RATE));
	*system = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_particles_reset(struct mh_particles *particles)
{
	if (particles != NULL)
		memset(particles, 0, sizeof(*particles));
}

static long double
random_signed(struct mh_particle_system *system)
{
	return ((long double)mh_random_next(&system->random) -
		RANDOM_MIDPOINT) /
		RANDOM_MIDPOINT;
}

static void
transform_point(const struct mh_particle_input *input, const float local[3],
	float world[3])
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		world[axis] = (float)((long double)local[1] *
				input->rotation[axis + 3] +
			(long double)local[0] * input->rotation[axis] +
			(long double)local[2] * input->rotation[axis + 6] +
			input->position[axis]);
}

static void
insert_particle(struct mh_particle_system *system,
	struct mh_particle_pool *pool, const struct mh_particle *source)
{
	struct mh_particle_animation *animation;
	struct mh_particle *particle;

	if (pool->count == MH_PARTICLE_CAPACITY)
		pool->first = (pool->first + 1) % MH_PARTICLE_CAPACITY;
	else
		pool->count++;
	particle = &pool->particles[(pool->first + pool->count - 1) %
		MH_PARTICLE_CAPACITY];
	*particle = *source;
	animation = &system->animations[particle->animation - 1];
	animation->cursor = animation->cursor < animation->frames - 1
		? animation->cursor + 1
		: 0;
	particle->frame = animation->cursor;
	particle->active = 1;
	particle->phase = PHASE_NEW;
	particle->flags = INITIAL_FLAGS;
	particle->remaining = particle->lifespan;
	pool->enabled = 1;
}

static void
emit_tyre(const struct mh_particle_input *input, unsigned int wheel,
	struct mh_particle_system *system, struct mh_particles *particles)
{
	struct mh_particle particle;
	float local[3];
	double spread, vertical, size, life;
	unsigned long id;

	id = input->force_smoke ? 1 : input->tyre_id[wheel];

	if (id < 1 || id > 3)
		return;

	memset(&particle, 0, sizeof(particle));

	/* Contact effect 3 also uses smoke, not the gravel animation. */
	particle.animation = id == 2 ? MH_PARTICLE_GRASS : MH_PARTICLE_SMOKE;
	particle.style = id == 2 ? 0 : MH_PARTICLE_STYLE_SMOKE;
	spread = id == 1 ? .3 : (id == 2 ? .1 : .45);
	vertical = id == 2 ? .1 : .15;
	size = id == 3 ? .2 : .1;
	life = id == 3 ? .3 : .2;
	local[0] = (float)(random_signed(system) * (long double)spread +
		input->wheels[wheel][0]);
	local[1] = (float)(random_signed(system) * (long double)vertical +
		input->wheels[wheel][1] + (id == 2 ? (long double)-.3 : 0));
	local[2] = (float)(random_signed(system) *
			(long double)(id == 3 ? .45 : .3) +
		input->wheels[wheel][2] + (id == 2 ? (long double)-.2 : -1));
	transform_point(input, local, particle.position);
	particle.width = (float)(random_signed(system) * (long double)size +
		(long double).3);
	particle.height = (float)(random_signed(system) * (long double)size +
		(long double).3);
	particle.lifespan =
		(float)(random_signed(system) * (long double)life + 1);

	if (id == 1) {
		particle.color[0] = particle.color[2] = 1.5f;
		particle.color[1] = system->hardware ? 1.4f : 1.5f;
	} else if (id == 2)
		particle.color[0] = particle.color[1] = particle.color[2] =
			system->hardware ? 1.2f : 1.3f;
	else {
		particle.color[0] = system->hardware ? .7f : 1;
		particle.color[1] = system->hardware ? .6f : .8f;
		particle.color[2] = system->hardware ? .6f : .4f;
	}

	insert_particle(system, &particles->pools[wheel], &particle);
}

static void
emit_spark(const struct mh_particle_input *input, unsigned int channel,
	struct mh_particle_system *system, struct mh_particles *particles)
{
	struct mh_particle particle;
	const float *corner;
	float local[3];
	long double offset;

	if (input->spark_id[channel] != 1 ||
		input->spark_gain[channel] < SPARK_GAIN_MIN)
		return;

	memset(&particle, 0, sizeof(particle));
	corner = input->corners[input->spark_corner[channel]];
	local[0] = corner[0];
	local[1] = (float)((long double)corner[1] + (long double)-.3);
	offset = (long double)mh_random_next(&system->random) /
		MH_RANDOM_RESULT_COUNT;
	local[2] = (float)(corner[2] > 0
			? (long double)corner[2] - offset + (long double)-.4
			: (long double)corner[2] + offset + (long double).4);
	transform_point(input, local, particle.position);
	particle.animation = MH_PARTICLE_SPARK;
	particle.style = local[0] > 0
		? MH_PARTICLE_STYLE_SPARK | MH_PARTICLE_STYLE_RIGHT
		: MH_PARTICLE_STYLE_SPARK;
	particle.width = particle.height =
		(float)((long double)input->spark_gain[channel] * .75L);
	particle.lifespan = .2f;
	particle.color[0] = particle.color[1] = particle.color[2] =
		system->hardware ? 6 : 1.75f;
	insert_particle(system, &particles->pools[SPARK_POOL_INDEX], &particle);
}

enum mh_error_code
mh_particles_emit(const struct mh_particle_input *input,
	struct mh_particle_system *system, struct mh_particles *particles,
	struct mh_error *error_out)
{
	unsigned int wheel, channel;

	if (!valid_system(system) || !valid_input(input) ||
		!valid_particles(particles, system))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid particle emitter input or state");

	for (wheel = 0; wheel < 2; wheel++)
		emit_tyre(input, wheel, system, particles);

	for (channel = 0; channel < 2; channel++)
		emit_spark(input, channel, system, particles);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static float
limit_growth(long double value)
{
	return value > (long double)GROWTH_LIMIT ? GROWTH_LIMIT : (float)value;
}

static void
advance_particle(const struct mh_particle_animation *animation,
	float elapsed_seconds, struct mh_particle *particle)
{
	long double remaining, frame;

	if (animation->flags & MH_PARTICLE_ANIM_SPARK) {
		if (particle->phase > PHASE_NEW) {
			particle->phase = PHASE_DONE;
			particle->active = 0;
			particle->remaining = 0;
		} else if (particle->phase == PHASE_NEW)
			particle->phase = PHASE_HELD;

		return;
	}

	remaining = (long double)particle->remaining - elapsed_seconds;
	particle->remaining = (float)remaining;

	if (remaining <= 0) {
		particle->active = 0;
		particle->remaining = 0;
	} else if (animation->mode == MH_PARTICLE_FRAME_ELAPSED) {
		frame = ((long double)particle->lifespan -
				particle->remaining) /
			animation->frame_seconds;
		particle->frame = frame >= animation->frames - 1
			? animation->frames - 1
			: (unsigned int)frame;
	}

	if (animation->flags & MH_PARTICLE_ANIM_GROW) {
		particle->position[1] = limit_growth(
			(long double)particle->position[1] + elapsed_seconds);
		particle->width = limit_growth((long double)particle->width +
			(long double)particle->width * .5L * elapsed_seconds);
		particle->height = limit_growth((long double)particle->height +
			(long double)particle->height * .5L * elapsed_seconds);
	}
}

enum mh_error_code
mh_particles_advance(const struct mh_particle_system *system,
	float elapsed_seconds, struct mh_particles *particles,
	struct mh_error *error_out)
{
	struct mh_particle_pool *pool;
	struct mh_particle *particle;
	const struct mh_particle_animation *animation;
	unsigned int pool_index, offset;

	if (!valid_system(system) || !valid_particles(particles, system) ||
		!(elapsed_seconds >= 0 && elapsed_seconds <= TIME_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid particle time or state");

	for (pool_index = 0; pool_index < MH_PARTICLE_POOLS; pool_index++) {
		pool = &particles->pools[pool_index];

		for (offset = 0; offset < pool->count; offset++) {
			particle = &pool->particles[(pool->first + offset) %
				MH_PARTICLE_CAPACITY];
			animation =
				&system->animations[particle->animation - 1];

			advance_particle(animation, elapsed_seconds, particle);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
