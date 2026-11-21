#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_presentation.h"

struct read_state {
	GLint framebuffer, read_buffer, buffer, alignment, row_length,
		skip_rows, skip_pixels;
};

static void
begin_read(struct read_state *state, GLuint framebuffer)
{
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state->framebuffer);
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &state->buffer);
	glGetIntegerv(GL_PACK_ALIGNMENT, &state->alignment);
	glGetIntegerv(GL_PACK_ROW_LENGTH, &state->row_length);
	glGetIntegerv(GL_PACK_SKIP_ROWS, &state->skip_rows);
	glGetIntegerv(GL_PACK_SKIP_PIXELS, &state->skip_pixels);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
	glGetIntegerv(GL_READ_BUFFER, &state->read_buffer);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_SKIP_ROWS, 0);
	glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
}

static void
end_read(const struct read_state *state)
{
	glReadBuffer((GLenum)state->read_buffer);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)state->framebuffer);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)state->buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, state->alignment);
	glPixelStorei(GL_PACK_ROW_LENGTH, state->row_length);
	glPixelStorei(GL_PACK_SKIP_ROWS, state->skip_rows);
	glPixelStorei(GL_PACK_SKIP_PIXELS, state->skip_pixels);
}

static void
top_down(struct mh_image *image)
{
	size_t row_bytes, x;
	unsigned int y;
	unsigned char *top, *bottom, swap;

	row_bytes = (size_t)image->width * 4;

	for (y = 0; y < image->height / 2; y++) {
		top = image->rgba + (size_t)y * row_bytes;
		bottom = image->rgba +
			(size_t)(image->height - 1 - y) * row_bytes;

		for (x = 0; x < row_bytes; x++) {
			swap = top[x];
			top[x] = bottom[x];
			bottom[x] = swap;
		}
	}
}

enum mh_error_code
mh_webgl_read_pixels(const struct mh_webgl_target *source,
	struct mh_image *output, struct mh_error *error_out)
{
	struct mh_image image;
	struct read_state state;
	enum mh_error_code status;

	if (source == NULL || source->framebuffer == 0 || output == NULL ||
		source->color.width == 0 || source->color.height == 0 ||
		source->color.width > (size_t)-1 / 4 / source->color.height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL readback request");

	image.width = source->color.width;
	image.height = source->color.height;
	image.rgba =
		(unsigned char *)malloc((size_t)image.width * image.height * 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL readback pixels");

	begin_read(&state, source->framebuffer);
	glReadPixels(0, 0, (GLsizei)image.width, (GLsizei)image.height, GL_RGBA,
		GL_UNSIGNED_BYTE, image.rgba);
	end_read(&state);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE) {
		free(image.rgba);
		return status;
	}

	top_down(&image);
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* WebGL 2 exposes getBufferSubData, which is not part of the GLES 3 C API. */
void	mh_webgl_download_bytes(unsigned char *output, size_t bytes);

static enum mh_error_code
resize_download(struct mh_webgl_download *download,
	const struct mh_webgl_target *source, struct mh_error *error_out)
{
	unsigned char *pixels;
	size_t bytes;
	enum mh_error_code status;

	if (download->image.width == source->color.width &&
		download->image.height == source->color.height)
		return MH_ERROR_NONE;

	bytes = (size_t)source->color.width * source->color.height * 4;
	pixels = malloc(bytes);

	if (pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL download pixels");

	glBufferData(
		GL_PIXEL_PACK_BUFFER, (GLsizeiptr)bytes, NULL, GL_STREAM_READ);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE) {
		free(pixels);
		return status;
	}

	free(download->image.rgba);
	download->image.rgba = pixels;
	download->image.width = source->color.width;
	download->image.height = source->color.height;

	return MH_ERROR_NONE;
}

enum mh_error_code
mh_webgl_download_queue(struct mh_webgl_download *download,
	const struct mh_webgl_target *source, struct mh_error *error_out)
{
	struct read_state state;
	enum mh_error_code status;

	if (download == NULL || download->fence != NULL || source == NULL ||
		source->framebuffer == 0 || source->color.width == 0 ||
		source->color.height == 0 ||
		source->color.width > (size_t)-1 / 4 / source->color.height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid queued WebGL download");

	begin_read(&state, source->framebuffer);

	if (download->buffer == 0)
		glGenBuffers(1, &download->buffer);

	if (download->buffer == 0) {
		end_read(&state);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot create WebGL download buffer");
	}

	glBindBuffer(GL_PIXEL_PACK_BUFFER, download->buffer);
	status = resize_download(download, source, error_out);

	if (status == MH_ERROR_NONE) {
		glReadPixels(0, 0, (GLsizei)source->color.width,
			(GLsizei)source->color.height, GL_RGBA,
			GL_UNSIGNED_BYTE, NULL);
		download->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		glFlush();
		status = mh_webgl_check_error(error_out);

		if (status == MH_ERROR_NONE && download->fence == NULL)
			status = mh_error_set(error_out, MH_ERROR_BACKEND,
				"cannot fence WebGL download");
	}

	end_read(&state);

	return status;
}

enum mh_error_code
mh_webgl_download_read(struct mh_webgl_download *download, int wait, int *ready,
	struct mh_error *error_out)
{
	GLint previous;
	GLenum state;
	enum mh_error_code status;

	if (download == NULL || ready == NULL || (wait != 0 && wait != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL download read");

	*ready = 0;

	if (download->fence == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!wait) {
		state = glClientWaitSync(download->fence, 0, 0);

		if (state == GL_TIMEOUT_EXPIRED)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (state != GL_ALREADY_SIGNALED &&
			state != GL_CONDITION_SATISFIED)
			return mh_error_set(error_out, MH_ERROR_BACKEND,
				"WebGL download fence failed");
	}

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, download->buffer);
	mh_webgl_download_bytes(download->image.rgba,
		(size_t)download->image.width * download->image.height * 4);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)previous);
	status = mh_webgl_check_error(error_out);

	if (status != MH_ERROR_NONE)
		return status;

	glDeleteSync(download->fence);
	download->fence = NULL;
	top_down(&download->image);
	*ready = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_webgl_download_destroy(struct mh_webgl_download *download)
{
	if (download == NULL)
		return;

	if (download->fence != NULL)
		glDeleteSync(download->fence);

	glDeleteBuffers(1, &download->buffer);
	free(download->image.rgba);
	memset(download, 0, sizeof(*download));
}
