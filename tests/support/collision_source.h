#ifndef MH_TEST_COLLISION_SOURCE_H
#define MH_TEST_COLLISION_SOURCE_H

#include "backends/storage/memory_resource.h"
#include "interfaces/collision/source_backend.h"

#define MH_TEST_COLLISIONS 256

/* Fixture-only alternate representation: a one-byte key into decoded data. */
struct mh_test_collision_source {
	struct mh_collision	collisions[MH_TEST_COLLISIONS];
	size_t			count, reads;
};

void	mh_test_collision_source_free(struct mh_test_collision_source *source);
void	mh_test_collision_source_init(struct mh_test_collision_source *source,
	struct mh_collision_source *decoder);

/*
 * Replaces a heap-owned COL entry with .fixture data; other entries stay
 * intact.
 */
enum mh_error_code	mh_test_collision_convert(
	struct mh_test_collision_source *source,
	struct mh_resource_entry *entry, struct mh_error *error_out);

#endif
