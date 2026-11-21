#ifndef MH_ENGINE_IMAGE_ATLAS_MAP_H
#define MH_ENGINE_IMAGE_ATLAS_MAP_H

#include "engine/core/error.h"
#include "interfaces/image/atlas_map.h"

#define MH_TEXTURE_GROUPS 64

struct mh_texture_groups {
	unsigned long	pages[MH_TEXTURE_GROUPS];
	unsigned int	count;
};

void	mh_atlas_map_free(struct mh_atlas_map *map);

enum mh_error_code	mh_atlas_map_entry(const struct mh_atlas_map *remap,
	unsigned long index, struct mh_atlas_map_entry *output,
	struct mh_error *error_out);

/* First full ASCII-insensitive name match; output unchanged if absent. */
enum mh_error_code	mh_atlas_map_find(const struct mh_atlas_map *remap,
	const char *name, struct mh_atlas_map_entry *output,
	struct mh_error *error_out);

/*
 * Initialize groups to zero, then visit named textures in original model
 * registration order. Full ASCII-insensitive names select atlas pages;
 * first-use pages get indices 0..63. Failure preserves both outputs.
 */
enum mh_error_code	mh_atlas_map_group(const struct mh_atlas_map *remap,
	const char *name, struct mh_texture_groups *groups, unsigned int *index,
	struct mh_error *error_out);

#endif
