#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_texture.h"
#include "engine/render/mipmap.h"

struct unpack_state {
	GLint	texture, buffer, alignment, row_length, skip_rows, skip_pixels;
};

static enum mh_error_code
validate_storage(unsigned int width, unsigned int height, unsigned int levels,
	GLenum format, struct mh_error *error_out)
{
	GLint maximum;

	if (width == 0 || height == 0 || levels == 0 ||
		levels > MH_TEXTURE_MIP_LEVELS ||
		(width >> (levels - 1)) == 0 || (height >> (levels - 1)) == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL texture dimensions or levels");

	if (format != GL_RGBA8 && format != GL_DEPTH_COMPONENT32F)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported WebGL texture format");

	maximum = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);

	if (maximum <= 0 || width > (unsigned int)maximum ||
		height > (unsigned int)maximum)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"texture exceeds WebGL device limits");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_texture_create(struct mh_webgl_texture *output, unsigned int width,
	unsigned int height, unsigned int levels, GLenum format,
	struct mh_error *error_out)
{
	GLuint handle;
	GLint previous;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL texture output");

	if (output->handle != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL texture is already created");

	status = validate_storage(width, height, levels, format, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	previous = 0;
	handle = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
	glGenTextures(1, &handle);
	glBindTexture(GL_TEXTURE_2D, handle);
	glTexStorage2D(GL_TEXTURE_2D, (GLsizei)levels, format, (GLsizei)width,
		(GLsizei)height);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)levels - 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, (GLuint)previous);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE || handle == 0) {
		glDeleteTextures(1, &handle);
		return status != MH_ERROR_NONE
			? status
			: mh_error_set(error_out, MH_ERROR_BACKEND,
				"cannot create WebGL texture");
	}

	output->handle = handle;
	output->width = width;
	output->height = height;
	output->levels = levels;
	output->format = format;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
begin_upload(struct unpack_state *state, GLuint texture)
{
	memset(state, 0, sizeof(*state));
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->texture);
	glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &state->buffer);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &state->alignment);
	glGetIntegerv(GL_UNPACK_ROW_LENGTH, &state->row_length);
	glGetIntegerv(GL_UNPACK_SKIP_ROWS, &state->skip_rows);
	glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &state->skip_pixels);
	glBindTexture(GL_TEXTURE_2D, texture);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
}

static void
end_upload(const struct unpack_state *state)
{
	glBindTexture(GL_TEXTURE_2D, (GLuint)state->texture);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)state->buffer);
	glPixelStorei(GL_UNPACK_ALIGNMENT, state->alignment);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, state->row_length);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, state->skip_rows);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, state->skip_pixels);
}

enum mh_error_code
mh_webgl_texture_upload(const struct mh_webgl_texture *texture,
	unsigned int level, const struct mh_image *image,
	struct mh_error *error_out)
{
	struct unpack_state state;

	if (texture == NULL || texture->handle == 0 ||
		texture->format != GL_RGBA8 || level >= texture->levels ||
		image == NULL || image->rgba == NULL ||
		image->width != (texture->width >> level) ||
		image->height != (texture->height >> level) ||
		image->height == 0 ||
		image->width > (size_t)-1 / 4 / image->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL texture upload");

	begin_upload(&state, texture->handle);
	glTexSubImage2D(GL_TEXTURE_2D, (GLint)level, 0, 0,
		(GLsizei)image->width, (GLsizei)image->height, GL_RGBA,
		GL_UNSIGNED_BYTE, image->rgba);
	end_upload(&state);

	return mh_webgl_check_error(error_out);
}

static enum mh_error_code
upload_mips(const struct mh_webgl_texture *texture,
	const struct mh_image *source, unsigned int format,
	struct mh_error *error_out)
{
	struct mh_image mip;
	unsigned int level;
	enum mh_error_code status;

	if (texture->levels == 1)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	mip.rgba = (unsigned char *)malloc(
		(size_t)(source->width >> 1) * (source->height >> 1) * 4);

	if (mip.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL mip pixels");

	status = MH_ERROR_NONE;

	for (level = 1; level < texture->levels && status == MH_ERROR_NONE;
		level++) {
		mip.width = source->width >> level;
		mip.height = source->height >> level;
		status = mh_mipmap_texture(
			source, format, level, &mip, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_webgl_texture_upload(
				texture, level, &mip, error_out);
	}

	free(mip.rgba);

	return status;
}

enum mh_error_code
mh_webgl_texture_from_image(struct mh_webgl_texture *output,
	const struct mh_image *image, unsigned int original_format,
	unsigned int levels, struct mh_error *error_out)
{
	struct mh_webgl_texture texture;
	enum mh_error_code status;

	if (output == NULL || image == NULL || image->rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL texture image or output");

	if (output->handle != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL texture is already created");

	memset(&texture, 0, sizeof(texture));
	status = mh_webgl_texture_create(&texture, image->width, image->height,
		levels, GL_RGBA8, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_texture_upload(&texture, 0, image, error_out);

	if (status == MH_ERROR_NONE)
		status = upload_mips(
			&texture, image, original_format, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_texture_destroy(&texture);
		return status;
	}

	*output = texture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_webgl_texture_destroy(struct mh_webgl_texture *texture)
{
	if (texture == NULL)
		return;

	glDeleteTextures(1, &texture->handle);
	memset(texture, 0, sizeof(*texture));
}
