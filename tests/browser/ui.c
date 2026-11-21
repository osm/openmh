#include <SDL3/SDL.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_context.h"
#include "backends/render/webgl_presentation.h"
#include "backends/render/webgl_provider.h"
#include "backends/render/webgl_ui.h"
#include "backends/window/sdl_provider.h"

static unsigned int failures;
static struct mh_ui_list list;

static void
mouse_input(const char *label)
{
	SDL_Event event;
	int pressed, wheel;

	while (SDL_PollEvent(&event)) {
	}

	emscripten_run_script(
		"(()=>{"
		"const canvas=document.getElementById('canvas');"
		"const bounds=canvas.getBoundingClientRect();"
		"for(const type of ['pointerdown','pointerup'])"
		"canvas.dispatchEvent(new PointerEvent(type,{button:0,pointerId:1,pointerType:'mouse',"
		"buttons:type==='pointerdown'?1:0,clientX:bounds.left+1,clientY:bounds.top+1,bubbles:true}));"
		"canvas.dispatchEvent(new WheelEvent('wheel',{deltaY:120,bubbles:true}));"
		"})()");
	pressed = 0;
	wheel = 0;

	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
			pressed = 1;

		if (event.type == SDL_EVENT_MOUSE_WHEEL)
			wheel = 1;
	}

	if (!pressed || !wheel) {
		fprintf(stderr, "%s failed\n", label);
		failures++;
	} else
		printf("%s\n", label);
}

static void
check(int success, const char *label)
{
	if (success)
		printf("%s\n", label);
	else {
		fprintf(stderr, "%s failed\n", label);
		failures++;
	}
}

static int
operation(enum mh_error_code status, const struct mh_error *error)
{
	if (status == MH_ERROR_NONE)
		return 1;

	fprintf(stderr, "%s\n", error->message);
	failures++;

	return 0;
}

static int
pixel(const struct mh_image *image, unsigned int x, unsigned int y, int r,
	int g, int b, int a)
{
	const unsigned char *p;

	p = image->rgba + ((size_t)y * image->width + x) * 4;

	return abs(p[0] - r) <= 1 && abs(p[1] - g) <= 1 && abs(p[2] - b) <= 1 &&
		abs(p[3] - a) <= 1;
}

static void
read_ui(const struct mh_webgl_target *target)
{
	struct mh_image image;
	struct mh_error error;
	GLuint buffer;
	GLint value;

	buffer = 0;
	glGenBuffers(1, &buffer);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
	glBufferData(GL_PIXEL_PACK_BUFFER, 1024, NULL, GL_STREAM_READ);
	glPixelStorei(GL_PACK_ALIGNMENT, 8);
	glPixelStorei(GL_PACK_ROW_LENGTH, 16);
	glPixelStorei(GL_PACK_SKIP_ROWS, 1);
	glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, target->framebuffer);
	glReadBuffer(GL_NONE);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	memset(&image, 0, sizeof(image));

	if (operation(mh_webgl_read_pixels(target, &image, &error), &error)) {
		check(image.width == 8 && image.height == 8,
			"readback retains logical dimensions");
		check(pixel(&image, 2, 2, 255, 0, 0, 255),
			"UI top-left atlas texel");
		check(pixel(&image, 5, 2, 0, 255, 0, 255),
			"UI top-right atlas texel");
		check(pixel(&image, 2, 5, 0, 0, 255, 255),
			"readback returns top-down rows");
		check(pixel(&image, 5, 5, 255, 255, 255, 255),
			"UI bottom-right atlas texel");
		check(pixel(&image, 0, 0, 0, 0, 0, 0),
			"UI preserves untouched pixels");
		free(image.rgba);
	}

	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &value);
	check(value == 0, "readback restores read framebuffer");
	glBindFramebuffer(GL_READ_FRAMEBUFFER, target->framebuffer);
	glGetIntegerv(GL_READ_BUFFER, &value);
	check(value == GL_NONE, "readback restores source read buffer");
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &value);
	check((GLuint)value == buffer, "readback restores pack buffer");
	glGetIntegerv(GL_PACK_ALIGNMENT, &value);
	check(value == 8, "readback restores pack alignment");
	glGetIntegerv(GL_PACK_ROW_LENGTH, &value);
	check(value == 16, "readback restores pack row length");
	glGetIntegerv(GL_PACK_SKIP_ROWS, &value);
	check(value == 1, "readback restores skipped rows");
	glGetIntegerv(GL_PACK_SKIP_PIXELS, &value);
	check(value == 2, "readback restores skipped pixels");
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glDeleteBuffers(1, &buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_SKIP_ROWS, 0);
	glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
}

static void
present(const struct mh_webgl_context *context,
	const struct mh_webgl_target *target)
{
	struct mh_error error;
	unsigned int width, height;
	unsigned char pixels[4];
	GLint value;

	if (!operation(mh_window_set_size(context->window, 16, 8, &error),
		&error) ||
		!operation(
			mh_webgl_context_size(context, &width, &height, &error),
			&error))
		return;

	check(width == 16 && height == 8, "context follows SDL window resize");
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->framebuffer);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glEnable(GL_SCISSOR_TEST);
	glScissor(0, 0, 1, 1);

	if (operation(
		mh_webgl_present(target, width, height, &error), &error)) {
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value);
		check((GLuint)value == target->framebuffer,
			"presentation restores draw framebuffer");
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &value);
		check(value == 0 && glIsEnabled(GL_SCISSOR_TEST),
			"presentation restores read framebuffer and scissor");
		glReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		check(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0,
			"presentation stretches top atlas pixels to resized canvas");
		glReadPixels(5, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		check(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 255,
			"presentation keeps bottom atlas pixels below top pixels");
	}

	glDisable(GL_SCISSOR_TEST);
}

static void
blend_and_validate(struct mh_webgl_ui *ui, const struct mh_ui_atlas *atlas,
	const struct mh_webgl_target *target)
{
	static const float translucent[] = {1, 1, 1, 0.5f};
	struct mh_ui_region region;
	struct mh_image image;
	struct mh_error error;
	GLuint program;

	region.x = region.y = 0;
	region.width = region.height = 1;
	mh_ui_begin(&list, 8, 8);
	mh_ui_quad_scaled(&list, atlas, &region, 0, 0, 8, 8, translucent);

	if (operation(mh_webgl_ui_draw(ui, &list, target, &error), &error) &&
		operation(
			mh_webgl_read_pixels(target, &image, &error), &error)) {
		check(pixel(&image, 0, 0, 128, 0, 0, 128),
			"UI alpha accumulates over transparent background");
		check(pixel(&image, 2, 5, 128, 0, 127, 255),
			"UI blends RGB over existing scene color");
		free(image.rgba);
	}

	list.count = 1;
	check(mh_webgl_ui_draw(ui, &list, target, &error) == MH_ERROR_INVALID,
		"UI rejects incomplete quads");
	list.count = MH_UI_MAX_QUADS * 6 + 6;
	check(mh_webgl_ui_draw(ui, &list, target, &error) == MH_ERROR_INVALID,
		"UI rejects oversized lists");
	list.count = 0;
	list.text_count = 1;
	check(mh_webgl_ui_draw(ui, &list, target, &error) ==
			MH_ERROR_UNSUPPORTED,
		"UI rejects software word fonts");
	list.text_count = 0;
	list.width = 4;
	check(mh_webgl_ui_draw(ui, &list, target, &error) == MH_ERROR_INVALID,
		"UI rejects mismatched logical dimensions");
	list.width = 8;
	check(mh_webgl_ui_draw(ui, &list, target, &error) == MH_ERROR_NONE,
		"empty UI frame succeeds");
	program = ui->program.handle;
	check(mh_webgl_ui_create(ui, atlas, 8, 8, &error) == MH_ERROR_STATE &&
			ui->program.handle == program,
		"UI refuses to overwrite live resources");
}

static void
draw_ui(const struct mh_webgl_context *context)
{
	static unsigned char pixels[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0,
		255, 255, 255, 255, 255, 255};

	static const float white[] = {1, 1, 1, 1};
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	struct mh_webgl_ui ui;
	struct mh_webgl_target target;
	struct mh_error error;
	GLuint program, texture, vertices, viewport, array, sampler;

	memset(&atlas, 0, sizeof(atlas));
	memset(&ui, 0, sizeof(ui));
	memset(&target, 0, sizeof(target));
	atlas.image.width = atlas.image.height = 2;
	atlas.image.rgba = pixels;

	if (!operation(mh_webgl_ui_create(&ui, &atlas, 8, 8, &error), &error) ||
		!operation(mh_webgl_target_create(&target, 8, 8, 1, &error),
			&error))
		goto cleanup;

	glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
	glClearColor(0, 0, 0, 0);
	glDepthMask(GL_TRUE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/*
	 * The preceding scene pass must not leak its draw state into the HUD.
	 */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_NEVER);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT_AND_BACK);
	glEnable(GL_SCISSOR_TEST);
	glScissor(0, 0, 0, 0);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	region.x = region.y = 0;
	region.width = region.height = 2;
	mh_ui_begin(&list, 8, 8);
	mh_ui_quad_scaled(&list, &atlas, &region, 2, 2, 4, 4, white);

	if (operation(mh_webgl_ui_draw(&ui, &list, &target, &error), &error)) {
		read_ui(&target);
		present(context, &target);
		blend_and_validate(&ui, &atlas, &target);
	}

cleanup:
	program = ui.program.handle;
	texture = ui.atlas.handle;
	vertices = ui.vertices;
	viewport = ui.viewport;
	array = ui.array.handle;
	sampler = ui.sampler;
	mh_webgl_ui_destroy(&ui);
	check(!glIsProgram(program) && !glIsTexture(texture) &&
			!glIsBuffer(vertices) && !glIsBuffer(viewport) &&
			!glIsVertexArray(array) && !glIsSampler(sampler),
		"UI releases owned GPU resources");
	mh_webgl_target_destroy(&target);
	mh_webgl_ui_destroy(&ui);
	check(glGetError() == GL_NO_ERROR,
		"UI cleanup is repeatable without GL errors");
}

static void
canvas_handoff(struct mh_window *window)
{
	static const unsigned char pixels[] = {255, 0, 0, 255, 0, 255, 0, 128,
		0, 0, 255, 255, 255, 255, 255, 255};

	static const struct mh_canvas_layout layout = {2, 2, 4, 4, 1, 1};
	struct mh_canvas *canvas;
	struct mh_webgl_context context;
	struct mh_image image;
	struct mh_error error;

	canvas = NULL;
	memset(&context, 0, sizeof(context));

	if (!operation(mh_canvas_open_layout(mh_webgl_canvas_provider(), window,
			&layout, &canvas, &error),
		&error))
		return;

	check(mh_webgl_context_open(&context, window, &error) == MH_ERROR_STATE,
		"WebGL rejects a window still owned by a canvas");

	if (operation(mh_canvas_draw(canvas, pixels, &error), &error) &&
		operation(mh_canvas_read(canvas, &image, &error), &error)) {
		check(image.width == 16 && image.height == 8,
			"canvas readback has drawable dimensions");
		check(pixel(&image, 6, 2, 255, 0, 0, 255) &&
				pixel(&image, 6, 5, 0, 0, 255, 255),
			"canvas retains top-down source orientation");
		check(pixel(&image, 9, 2, 0, 255, 0, 128),
			"canvas copies source color without alpha blending");
		check(pixel(&image, 0, 2, 0, 0, 0, 255),
			"canvas adds black side bars");
		check(pixel(&image, 4, 2, 0, 0, 0, 255) &&
				pixel(&image, 6, 1, 0, 0, 0, 255),
			"canvas retains source placement inside its logical screen");
		free(image.rgba);
	}

	operation(mh_canvas_present(canvas, &error), &error);

	if (operation(mh_window_set_size(window, 8, 16, &error), &error) &&
		operation(mh_canvas_draw(canvas, pixels, &error), &error) &&
		operation(mh_canvas_read(canvas, &image, &error), &error)) {
		check(image.width == 8 && image.height == 16,
			"canvas recreates its target after resize");
		check(pixel(&image, 2, 6, 255, 0, 0, 255) &&
				pixel(&image, 2, 9, 0, 0, 255, 255),
			"canvas retains source dimensions when fitting a tall window");
		check(pixel(&image, 2, 0, 0, 0, 0, 255),
			"canvas adds black top bars");
		free(image.rgba);
	}

	mh_canvas_destroy(canvas);
	mouse_input("SDL receives mouse clicks and wheel after canvas closes");

	if (operation(
		mh_webgl_context_open(&context, window, &error), &error)) {
		check(mh_webgl_context_current(&context, &error) ==
				MH_ERROR_NONE,
			"WebGL reacquires the window after canvas closes");
		draw_ui(&context);
		mh_webgl_context_close(&context);
	}
}

int
mh_webgl_ui_check(void)
{
	const struct mh_window_system *system;
	struct mh_window *window;
	struct mh_webgl_context context, duplicate;
	struct mh_error error;

	memset(&context, 0, sizeof(context));
	memset(&duplicate, 0, sizeof(duplicate));
	system = mh_sdl_window_system();
	window = NULL;

	if (!operation(mh_window_system_acquire(system, NULL, &error), &error))
		return 1;

	if (!operation(mh_window_open(system, "WebGL UI check", 8, 8, 0,
			&window, &error),
		&error))
		goto cleanup;

	if (!operation(mh_webgl_context_open(&context, window, &error), &error))
		goto cleanup;

	check(mh_webgl_context_open(&duplicate, window, &error) ==
				MH_ERROR_STATE &&
			duplicate.handle == 0,
		"WebGL prevents duplicate window ownership");
	mouse_input("SDL receives mouse clicks while WebGL is active");
	draw_ui(&context);
	mh_webgl_context_close(&context);
	mouse_input("SDL receives mouse clicks after WebGL closes");
	check(mh_webgl_context_current(&context, &error) == MH_ERROR_STATE,
		"closed WebGL context cannot become current");
	canvas_handoff(window);
	canvas_handoff(window);

cleanup:
	mh_webgl_context_close(&context);
	mh_window_destroy(window);
	check(emscripten_webgl_get_current_context() == 0,
		"window destruction releases its WebGL context");
	mh_window_system_release(system);

	return failures != 0;
}

int
main(void)
{
	return 0;
}
