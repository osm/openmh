#ifndef MH_TEST_ANIMATION_SOURCE_H
#define MH_TEST_ANIMATION_SOURCE_H

#include "backends/storage/memory_resource.h"
#include "interfaces/animation/source_backend.h"

#define MH_TEST_ANIMATIONS 64

/* Two-byte resource keys select owned templates; no original bytes remain. */
struct mh_test_animation_source {
	struct mh_scene	scenes[MH_TEST_ANIMATIONS];
	int		motion[MH_TEST_ANIMATIONS];
	size_t		count, scene_reads, motion_reads;
};

void			mh_test_animation_source_init(
	struct mh_test_animation_source *source,
	struct mh_animation_source *provider);
void			mh_test_animation_source_free(
	struct mh_test_animation_source *source);
enum mh_error_code	mh_test_animation_convert(
	struct mh_test_animation_source *source,
	struct mh_resource_entry *entry, int motion,
	struct mh_error *error_out);

#endif
