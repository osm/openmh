#ifndef MH_TEST_SURFACE_SOURCE_H
#define MH_TEST_SURFACE_SOURCE_H

#include "interfaces/audio/environment_source_backend.h"
#include "interfaces/material/source_backend.h"

#define MH_TEST_MATERIAL_REFERENCE "track/surfaces.converted"
#define MH_TEST_ENVIRONMENT_REFERENCE "track/sounds.converted"

/* Decoded track definitions supplied without encoded files. */
struct mh_test_surface_source {
	struct mh_materials	materials;
	struct mh_env_sounds	environment;
	unsigned int		material_reads, environment_reads;
	int			fail;
};

void	mh_test_surface_sources_init(struct mh_test_surface_source *source,
	struct mh_material_source *materials,
	struct mh_env_sound_source *environment);

#endif
