#include <stdlib.h>

#include "engine/core/error.h"
#include "engine/render/viewport.h"
#include "engine/window/graphics_catalog.h"
#include "interfaces/window/window.h"

#define MODE_LIMIT 4096

struct mh_graphics_catalog {
	struct mh_menu_graphics_catalog	menu;
	struct mh_menu_graphics_device	devices[MH_MENU_GRAPHICS_DEVICES];
	struct mh_menu_graphics_mode	modes[MODE_LIMIT];
	unsigned int			mode_count;
};

static int
valid_size(unsigned long width, unsigned long height)
{
	return width >= MH_VIEW_MIN_WIDTH && width <= MH_VIEW_MAX_WIDTH &&
		height >= MH_VIEW_MIN_HEIGHT && height <= MH_VIEW_MAX_HEIGHT;
}

static void
append_mode(struct mh_graphics_catalog *catalog, unsigned long width,
	unsigned long height)
{
	unsigned int i;

	if (!valid_size(width, height) || catalog->mode_count == MODE_LIMIT)
		return;

	for (i = 0; i < catalog->mode_count; i++)
		if (catalog->modes[i].width == width &&
			catalog->modes[i].height == height)
			return;

	catalog->modes[catalog->mode_count].width = width;
	catalog->modes[catalog->mode_count].height = height;
	catalog->mode_count++;
}

static int
compare_modes(const void *left, const void *right)
{
	const struct mh_menu_graphics_mode *first, *second;

	first = (const struct mh_menu_graphics_mode *)left;
	second = (const struct mh_menu_graphics_mode *)right;

	if (first->width != second->width)
		return first->width < second->width ? -1 : 1;

	if (first->height != second->height)
		return first->height < second->height ? -1 : 1;

	return 0;
}

static int
visit_mode(void *context, unsigned int width, unsigned int height)
{
	append_mode(context, width, height);

	return 1;
}

static void
bind_renderer(struct mh_graphics_catalog *catalog, unsigned int index,
	const struct mh_graphics_renderer *renderer)
{
	struct mh_menu_graphics_device *device;
	unsigned int modes;

	device = &catalog->devices[index];
	device->name = renderer->name;
	device->modes = catalog->modes;
	device->glenz = renderer->glenz;
	device->motion_blur = renderer->motion_blur;
	device->texture_precision = renderer->texture_precision;
	device->frame_queue = renderer->frame_queue;
	device->blur_history = renderer->blur_history;
	device->texture_filter = renderer->texture_filter;

	/* Width ordering makes every supported list a shared prefix. */
	for (modes = 0; modes < catalog->mode_count &&
		catalog->modes[modes].width <= renderer->max_width;
		modes++)
		;

	device->count = modes;
}

enum mh_error_code
mh_graphics_catalog_open_from(const struct mh_window_system *system,
	unsigned long width, unsigned long height,
	const struct mh_graphics_renderer *renderers, unsigned int count,
	unsigned int device, int presentation, int screenshots,
	struct mh_graphics_catalog **output, unsigned int *selected,
	struct mh_error *error_out)
{
	static const unsigned int common_sizes[][2] = {{320, 200}, {320, 240},
		{640, 400}, {640, 480}, {800, 600}, {960, 640}, {1024, 768},
		{1280, 720}, {1280, 800}, {1280, 960}, {1280, 1024},
		{1366, 768}, {1600, 900}, {1920, 1080}, {1920, 1200},
		{2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 2160}};

	struct mh_graphics_catalog *catalog;
	unsigned int i, fallback;

	if (system == NULL || output == NULL || selected == NULL ||
		renderers == NULL || count == 0 ||
		count > MH_MENU_GRAPHICS_DEVICES || device >= count ||
		(presentation != 0 && presentation != 1) ||
		(screenshots != 0 && screenshots != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid graphics catalog output");

	for (i = 0; i < count; i++)
		if (renderers[i].name == NULL || renderers[i].name[0] == 0 ||
			(renderers[i].max_width != 0 &&
				renderers[i].max_width < MH_VIEW_MIN_WIDTH) ||
			renderers[i].max_width > MH_VIEW_MAX_WIDTH ||
			(renderers[i].glenz != 0 && renderers[i].glenz != 1) ||
			(renderers[i].motion_blur != 0 &&
				renderers[i].motion_blur != 1) ||
			(renderers[i].texture_precision != 0 &&
				renderers[i].texture_precision != 1) ||
			(renderers[i].blur_history != 0 &&
				renderers[i].blur_history != 1) ||
			(renderers[i].frame_queue != 0 &&
				renderers[i].frame_queue != 1) ||
			(renderers[i].texture_filter != 0 &&
				renderers[i].texture_filter != 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid graphics renderer description");

	catalog = (struct mh_graphics_catalog *)calloc(1, sizeof(*catalog));

	if (catalog == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory enumerating graphics modes");

	catalog->menu.devices = catalog->devices;
	catalog->menu.fullscreen = mh_window_system_has_fullscreen(system);
	catalog->menu.presentation = presentation;
	catalog->menu.screenshots = screenshots;
	catalog->menu.count = count;

	if (!valid_size(width, height)) {
		width = MH_VIEW_WIDTH;
		height = MH_VIEW_HEIGHT;
	}

	/* Reserve the selected size before accepting bounded platform input. */
	append_mode(catalog, width, height);

	for (i = 0; i < sizeof(common_sizes) / sizeof(common_sizes[0]); i++)
		append_mode(catalog, common_sizes[i][0], common_sizes[i][1]);

	/*
	 * Enumeration failure retains common sizes and any visited display
	 * sizes.
	 */
	(void)mh_window_system_modes(
		system, MODE_LIMIT + 1, visit_mode, catalog, NULL);

	qsort(catalog->modes, catalog->mode_count, sizeof(catalog->modes[0]),
		compare_modes);

	for (i = 0; i < count; i++)
		bind_renderer(catalog, i, &renderers[i]);

	fallback = 0;

	for (i = 0; i < catalog->devices[device].count; i++) {
		if (catalog->modes[i].width == MH_VIEW_WIDTH &&
			catalog->modes[i].height == MH_VIEW_HEIGHT)
			fallback = i;

		if (catalog->modes[i].width == width &&
			catalog->modes[i].height == height)
			break;
	}

	if (i == catalog->devices[device].count)
		i = fallback;

	*output = catalog;
	*selected = i;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_menu_graphics_catalog *
mh_graphics_catalog_choices(const struct mh_graphics_catalog *catalog)
{
	return catalog == NULL ? NULL : &catalog->menu;
}

void
mh_graphics_catalog_close(struct mh_graphics_catalog *catalog)
{
	free(catalog);
}
