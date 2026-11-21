#ifndef MH_WEBGL_IMAGES_H
#define MH_WEBGL_IMAGES_H

#include "backends/render/webgl_texture.h"
#include "engine/render/mesh.h"

#define MH_WEBGL_NO_IMAGE ((size_t)-1)

struct mh_webgl_image {
	struct mh_webgl_texture	texture;
	const struct mh_image	*source;
	unsigned int		format, levels;
};

struct mh_webgl_images {
	struct mh_webgl_image	*items;
	size_t			**bindings;
	size_t			count, mesh_count;
	int			powervr;
};

/*
 * Own shared textures and each mesh's texture-slot indices. Borrow immutable
 * mesh/image sources through destroy. Deduplicate by pixel storage and reject
 * inconsistent dimensions/formats. Plan mip requirements across all meshes
 * before uploading. Zero-initialize output; failure leaves it unchanged.
 */
enum mh_error_code	mh_webgl_images_create(struct mh_webgl_images *output,
	const struct mh_mesh *const *meshes, size_t count, int powervr,
	struct mh_error *error_out);
void			mh_webgl_images_destroy(struct mh_webgl_images *images);

/*
 * Release/recreate GPU textures while retaining source and binding ownership.
 */
void			mh_webgl_images_release(struct mh_webgl_images *images);
enum mh_error_code	mh_webgl_images_upload(struct mh_webgl_images *images,
	struct mh_error *error_out);

#endif
