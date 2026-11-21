#ifndef MH_PARTICLES_H
#define MH_PARTICLES_H

#include "engine/core/error.h"
#include "engine/simulation/body_points.h"

#define MH_PARTICLE_CAPACITY 16
#define MH_PARTICLE_POOLS 3
#define MH_PARTICLE_ANIMATIONS 4

enum mh_particle_animation_id {
	MH_PARTICLE_SMOKE = 1,
	MH_PARTICLE_GRASS = 2,
	MH_PARTICLE_GRAVEL = 3,
	MH_PARTICLE_SPARK = 4
};

enum mh_particle_style {
	MH_PARTICLE_STYLE_SPARK = 1,
	MH_PARTICLE_STYLE_RIGHT = 2,
	MH_PARTICLE_STYLE_SMOKE = 4
};

enum mh_particle_flag {
	MH_PARTICLE_FADE = 0x40
};

enum mh_particle_animation_flag {
	MH_PARTICLE_ANIM_SPARK = 1,
	MH_PARTICLE_ANIM_GROW = 4
};

enum mh_particle_frame_mode {
	MH_PARTICLE_FRAME_ELAPSED = 0,
	MH_PARTICLE_FRAME_KEEP = 1
};

struct mh_particle_animation {
	unsigned int	flags, mode, frames, cursor;
	float		frame_seconds;
};

struct mh_particle_system {
	unsigned long			random;
	int				hardware;
	struct mh_particle_animation	animations[MH_PARTICLE_ANIMATIONS];
};

struct mh_particle {
	unsigned int	flags, style, phase, active;
	float	lifespan, remaining, position[3], color[3], width, height;

	/* Portable 1-based ID, not a pointer. */
	unsigned int	frame, animation;
};

struct mh_particle_pool {
	unsigned int		enabled, count, first;
	struct mh_particle	particles[MH_PARTICLE_CAPACITY];
};

struct mh_particles {
	/* Two rear tyre pools, then shared sparks. */
	struct mh_particle_pool	pools[MH_PARTICLE_POOLS];
};

struct mh_particle_input {
	unsigned long	tyre_id[2], spark_id[2];
	float		spark_gain[2];
	unsigned int	spark_corner[2];

	/* Render-local attachment positions. */
	float		wheels[2][3], corners[MH_CAR_BODY_POINTS][3];

	/* Column-major render transform. */
	float		rotation[9], position[3];

	/* Selected underwater renderer flag. */
	int		force_smoke;
};

/*
 * Shared animation cursors and random stream belong to the field, while each
 * car owns three pools. No allocation or mutable globals. Init accepts hardware
 * 0/1 and a 32-bit seed. Reset only clears a car.
 */
enum mh_error_code	mh_particle_system_init(
	struct mh_particle_system *system, int hardware, unsigned long seed,
	struct mh_error *error_out);
void			mh_particles_reset(struct mh_particles *particles);

/*
 * Particle emission with ring insertion. Tyre IDs 1..3 emit; only spark ID 1
 * with gain >= .5 emits. Unknown IDs are ignored. Local/input position/gain
 * bounds +/-1e6, rotation +/-2, corner 0..6. Both outputs remain unchanged on
 * invalid input. Does not test active/available gates: the caller applies
 * per-player eligibility before calling.
 */
enum mh_error_code	mh_particles_emit(const struct mh_particle_input *input,
	struct mh_particle_system *system, struct mh_particles *particles,
	struct mh_error *error_out);

/*
 * Advances every occupied slot, including inactive particles and disabled
 * pools, just like the original pointer-list updater. Time is 0..10s. Smoke
 * rises and grows; grass stays fixed; sparks expire on update two. Invisible
 * expired smoke growth is capped at 1e30 to avoid eventual float overflow. No
 * other decay or compaction is added. Output atomic on error.
 */
enum mh_error_code	mh_particles_advance(
	const struct mh_particle_system *system, float elapsed_seconds,
	struct mh_particles *particles, struct mh_error *error_out);

#endif
