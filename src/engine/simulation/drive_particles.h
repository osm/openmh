#ifndef MH_DRIVE_PARTICLES_H
#define MH_DRIVE_PARTICLES_H

#include "engine/core/error.h"
#include "engine/simulation/particles.h"

struct mh_drive;

/*
 * Bridge the current display pose, CAR attachments and local/received effect
 * channels into the emitter. Local sparks use frame body feedback; remote
 * sparks use received IDs/gains/corners. Eligibility remains caller-owned.
 */
enum mh_error_code	mh_drive_particles_emit(const struct mh_drive *drive,
	struct mh_particle_system *system, struct mh_particles *particles,
	struct mh_error *error_out);

#endif
