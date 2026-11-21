#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_images.h"
#include "engine/render/mipmap.h"
#include "engine/render/powervr_texture.h"

static enum mh_error_code
allocate_pool(struct mh_webgl_images *pool, const struct mh_mesh *const *meshes,
	size_t count, struct mh_error *error_out)
{
	size_t i, capacity;

	capacity = 0;

	if (count > (size_t)-1 / sizeof(*pool->bindings))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"too many WebGL image bindings");

	for (i = 0; i < count; i++) {
		if (meshes[i] == NULL || meshes[i]->texture_count == 0 ||
			(meshes[i]->draw_count != 0 &&
				meshes[i]->draws == NULL))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing WebGL mesh images or draws");

		if (meshes[i]->texture_count >
				(size_t)-1 / sizeof(*pool->items) - capacity ||
			meshes[i]->texture_count >
				(size_t)-1 / sizeof(**pool->bindings))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many shared WebGL images");

		capacity += meshes[i]->texture_count;
	}

	pool->items = calloc(capacity ? capacity : 1, sizeof(*pool->items));
	pool->bindings = calloc(count ? count : 1, sizeof(*pool->bindings));

	if (pool->items == NULL || pool->bindings == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL image pool");

	pool->mesh_count = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
bind_mesh(struct mh_webgl_images *pool, const struct mh_mesh *mesh,
	size_t model, struct mh_error *error_out)
{
	const struct mh_image *source;
	struct mh_webgl_image *image;
	size_t texture, index;
	unsigned int format;

	pool->bindings[model] =
		malloc(mesh->texture_count * sizeof(**pool->bindings));

	if (pool->bindings[model] == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot bind WebGL mesh images");

	for (texture = 0; texture < mesh->texture_count; texture++) {
		if (MH_RENDER_BUILD_POWERVR && pool->powervr &&
			mh_powervr_texture_unused(mesh, texture)) {
			pool->bindings[model][texture] = MH_WEBGL_NO_IMAGE;
			continue;
		}

		source = mh_mesh_texture(mesh, texture);

		if (source == NULL || source->rgba == NULL ||
			source->width == 0 || source->height == 0 ||
			source->width > INT_MAX / 4 / source->height)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid WebGL image source");

		format = mh_mesh_texture_format(mesh, texture);

		for (index = 0; index < pool->count &&
			source->rgba != pool->items[index].source->rgba;
			index++) {
		}

		image = &pool->items[index];

		if (index == pool->count) {
			image->source = source;
			image->format = format;
			image->levels = 1;
			pool->count++;
		} else if (source->width != image->source->width ||
			source->height != image->source->height ||
			format != image->format)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"inconsistent shared WebGL image format or dimensions");

		pool->bindings[model][texture] = index;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
plan_mips(struct mh_webgl_images *pool, const struct mh_mesh *mesh,
	size_t model, struct mh_error *error_out)
{
	struct mh_webgl_image *image;
	size_t i;
	unsigned int width, height, level;

	for (i = 0; i < mesh->draw_count; i++) {
		if (mesh->draws[i].texture >= mesh->texture_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid WebGL draw texture");

		if (pool->powervr || mesh->draws[i].environment)
			continue;

		image = &pool->items[pool
				->bindings[model][mesh->draws[i].texture]];
		width = image->source->width;
		height = image->source->height;

		for (level = 1; level < MH_TEXTURE_MIP_LEVELS && width >= 2 &&
			height >= 2;
			level++) {
			width >>= 1;
			height >>= 1;
		}

		image->levels = level;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#if MH_RENDER_BUILD_POWERVR
static enum mh_error_code
upload_powervr(struct mh_webgl_image *image, struct mh_error *error_out)
{
	const struct mh_image *source;
	struct mh_image prepared;
	unsigned int side;
	enum mh_error_code status;

	source = image->source;

	/* The untextured white pixel is synthetic, not a PowerSGL upload. */
	if (image->format == 0 && source->width == 1 && source->height == 1 &&
		source->rgba[0] == 255 && source->rgba[1] == 255 &&
		source->rgba[2] == 255 && source->rgba[3] == 255)
		return mh_webgl_texture_from_image(
			&image->texture, source, 0, 1, error_out);

	if (image->format != MH_TEXTURE_RGB555 &&
		image->format != MH_TEXTURE_ARGB4444)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"PowerVR requires RGB555 or ARGB4444 texture preparation");

	status = mh_powervr_texture_size(
		source->width, source->height, &side, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	prepared.width = prepared.height = side;
	prepared.rgba = malloc((size_t)side * side * 4);

	if (prepared.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate PowerVR texture preparation");

	status = mh_powervr_texture_prepare(source,
		(enum mh_texture_format)image->format, &prepared, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_texture_from_image(&image->texture, &prepared,
			image->format, 1, error_out);

	free(prepared.rgba);

	return status;
}
#endif

void
mh_webgl_images_release(struct mh_webgl_images *images)
{
	size_t i;

	for (i = 0; i < images->count; i++)
		mh_webgl_texture_destroy(&images->items[i].texture);
}

enum mh_error_code
mh_webgl_images_upload(
	struct mh_webgl_images *images, struct mh_error *error_out)
{
	enum mh_error_code status;
	size_t i;

	status = MH_ERROR_NONE;

	for (i = 0; status == MH_ERROR_NONE && i < images->count; i++) {
#if MH_RENDER_BUILD_POWERVR
		if (images->powervr) {
			status = upload_powervr(&images->items[i], error_out);
			continue;
		}

#endif
		status = mh_webgl_texture_from_image(&images->items[i].texture,
			images->items[i].source, images->items[i].format,
			images->items[i].levels, error_out);
	}

	return status;
}

enum mh_error_code
mh_webgl_images_create(struct mh_webgl_images *output,
	const struct mh_mesh *const *meshes, size_t count, int powervr,
	struct mh_error *error_out)
{
	struct mh_webgl_images pool;
	enum mh_error_code status;
	size_t i;

	if (output == NULL || (meshes == NULL && count != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL image pool inputs");

	if (output->items != NULL || output->bindings != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL image pool is already created");

	memset(&pool, 0, sizeof(pool));
	pool.powervr = powervr;
	status = allocate_pool(&pool, meshes, count, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < count; i++)
		status = bind_mesh(&pool, meshes[i], i, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < count; i++)
		status = plan_mips(&pool, meshes[i], i, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_images_upload(&pool, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_images_destroy(&pool);
		return status;
	}

	*output = pool;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_webgl_images_destroy(struct mh_webgl_images *images)
{
	size_t i;

	if (images == NULL)
		return;

	mh_webgl_images_release(images);

	for (i = 0; i < images->mesh_count; i++)
		free(images->bindings[i]);

	free(images->items);
	free(images->bindings);
	memset(images, 0, sizeof(*images));
}
