#ifndef MH_TEST_GEOMETRY_SOURCE_H
#define MH_TEST_GEOMETRY_SOURCE_H

#include "backends/storage/memory_resource.h"
#include "interfaces/geometry/decoder_backend.h"

#define MH_TEST_GEOMETRY_ENTRIES 512

/* Fixture-only formats: two-byte keys into independently decoded templates. */
struct mh_test_geometry_source {
	struct mh_model	models[MH_TEST_GEOMETRY_ENTRIES];
	struct mh_world	worlds[MH_TEST_GEOMETRY_ENTRIES];
	size_t		model_count, world_count, model_reads, world_reads;
};

void	mh_test_geometry_source_free(struct mh_test_geometry_source *source);
void	mh_test_geometry_decoders(struct mh_test_geometry_source *source,
	struct mh_model_decoder *models, struct mh_world_decoder *worlds);

/* Replace heap-owned MYO/MYW entries; all other entries remain unchanged. */
enum mh_error_code	mh_test_geometry_convert(
	struct mh_test_geometry_source *source, struct mh_resource_entry *entry,
	struct mh_error *error_out);

#endif
