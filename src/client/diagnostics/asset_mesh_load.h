#ifndef MH_CLIENT_DIAGNOSTICS_ASSET_MESH_LOAD_H
#define MH_CLIENT_DIAGNOSTICS_ASSET_MESH_LOAD_H

#include "engine/assets/mesh_load.h"

/*
 * Native mesh loaders; select the filesystem resource source.
 * Model/image ownership, world selection and coordinate rules follow the
 * corresponding portable _from operations.
 */
enum mh_error_code	mh_car_mesh_load(const char *root, unsigned int number,
	const struct mh_image_collection *collection, struct mh_mesh *mesh,
	char name[128], struct mh_error *error_out);

enum mh_error_code	mh_mesh_environment_load(const char *root,
	unsigned int number, struct mh_mesh *mesh, struct mh_error *error_out);

enum mh_error_code	mh_world_mesh_load(const char *root,
	unsigned int number, int standard_world, int scenery,
	int original_units, const struct mh_image_collection *collection,
	struct mh_mesh *mesh, char name[256], struct mh_error *error_out);

#endif
