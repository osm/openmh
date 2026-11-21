#ifndef MH_COLLISION_H
#define MH_COLLISION_H

#include "engine/core/error.h"
#include "interfaces/collision/data.h"

struct mh_contact {
	int		hit;
	unsigned long	polygon;
	unsigned long	material;

	/* Along start -> end, before the original normal offset. */
	double		fraction;
	double		position[3];
	double		normal[3];
	double		plane_distance;

	/* Includes repeated references in different leaves. */
	size_t		polygon_tests;
};

/*
 * One-sided segment query with plane/edge tests. Traverses integer cells and
 * returns the first accepted cell's winner. Within its stored polygon list,
 * each unrounded candidate quotient is compared strictly against the stored
 * winning fraction. Query endpoints, plane distances, fractions and hit
 * positions use binary32 stores, with binary53 intermediates. Coordinates are
 * world units. No allocations or global mutable state. Decoded collision data
 * must stay alive and unchanged. Success includes misses; failures preserve
 * output. Inputs must be finite in [-1e6,1e6], and stored grid coordinates must
 * fit signed 32-bit conversion. Zero-length segments miss.
 */
enum mh_error_code	mh_collision_cast(const struct mh_collision *col,
	const double start[3], const double end[3], struct mh_contact *contact,
	struct mh_error *error_out);

#endif
