#include <string.h>

#include "backends/render/software_blur.h"
#include "backends/render/software_renderer_internal.h"

static int
draw_triangles(
	struct mh_software_renderer *software, size_t first, size_t count)
{
	struct mh_software_draw_triangle *triangle;
	size_t i;

	for (i = first; i < first + count; i++) {
		triangle = &software->triangles[i];

		if (triangle->rejected)
			continue;

		if (!mh_software_triangle_bind_texture(software, triangle) ||
			mh_software_triangle(&software->work, &triangle->raster,
				&software->texture_state,
				NULL) != MH_ERROR_NONE)
			return 0;
	}

	return 1;
}

static void
retain_row_tails(struct mh_software_surface *work,
	const struct mh_software_surface *published)
{
	size_t bytes, tail;
	unsigned int y;

	/* Patched blur copies complete visible dwords to the display. */
	bytes = (size_t)work->width * 2;
	bytes -= bytes % 4;
	tail = work->pitch - bytes;

	if (tail == 0)
		return;

	for (y = 0; y < work->height; y++)
		memcpy(work->pixels + (size_t)y * work->pitch + bytes,
			published->pixels + (size_t)y * published->pitch +
				bytes,
			tail);
}

static int
draw_scene(struct mh_software_renderer *software, size_t span_count,
	int background, int blur, const struct mh_ui_list *ui)
{
	const struct mh_software_order_span *span;
	const struct mh_software_halo_entry *entry;
	struct mh_software_texture_state saved_texture;
	unsigned char *pixels;
	size_t i, halo, marker_count;

	saved_texture = software->texture_state;
	marker_count = 0;

	if (!background)
		memset(software->work.pixels, 0, software->work.size);

	for (i = 0; i < span_count; i++) {
		span = &software->spans[i];
		halo = software->triangles[span->first].halo;

		if (halo != 0) {
			entry = &software->halos.entries[halo - 1];

			if (mh_software_halo_mark(&software->work,
				entry->geometry.marker[0],
				entry->geometry.marker[1],
				&software->halos.markers[marker_count++],
				NULL) != MH_ERROR_NONE)
				goto failed;
		} else if (!draw_triangles(software, span->first, span->count))
			goto failed;
	}

	/* Restore every visible marker before drawing ANY glow or lens quad. */
	if (marker_count != 0 &&
		mh_software_halo_resolve(&software->work,
			software->halos.markers, marker_count,
			NULL) != MH_ERROR_NONE)
		goto failed;

	for (i = 0; i < marker_count; i++) {
		if (!software->halos.markers[i].visible)
			continue;

		entry = &software->halos.entries[software->halos.draw_order[i]];

		if (!draw_triangles(software, entry->first, entry->count))
			goto failed;
	}

	if (blur) {
		if (mh_software_blur(&software->work,
			software->history[software->history_next ^ 1],
			software->work.size, NULL) != MH_ERROR_NONE)
			goto failed;

		/* Keep UI out of the accumulated scene history. */
		memcpy(software->history_pending, software->work.pixels,
			software->work.size);

		retain_row_tails(&software->work, &software->surface);
	}

	mh_software_ui_draw(&software->ui, &software->work, ui);

	if (!mh_software_present(software->presentation, &software->work))
		goto failed;

	if (blur) {
		pixels = software->history[software->history_next];
		software->history[software->history_next] =
			software->history_pending;
		software->history_pending = pixels;
		software->history_next ^= 1;
	}

	pixels = software->surface.pixels;
	software->surface.pixels = software->work.pixels;
	software->work.pixels = pixels;
	software->frame_ready = 1;
	return 1;

failed:
	software->texture_state = saved_texture;

	return 0;
}

int
mh_software_renderer_scene_frame_matrix(struct mh_renderer *renderer,
	const float matrix[16], const float view[16], const float *models,
	int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro, struct mh_error *error_out)
{
	struct mh_software_renderer *software;
	size_t span_count;

	mh_error_set(
		error_out, MH_ERROR_BACKEND, "software matrix frame failed");
	(void)view;
	software = (struct mh_software_renderer *)renderer;

	if (reflections || retro != NULL)
		return 0;

	if (mh_software_ui_check(&software->ui, &software->work, ui,
		error_out) != MH_ERROR_NONE)
		return 0;

	mh_error_set(
		error_out, MH_ERROR_BACKEND, "software matrix frame failed");

	/*
	 * Results replace the frame while keeping race meshes and sky resident.
	 */
	if (matrix == NULL)
		return draw_scene(software, 0, 0, 0, ui);

	if (software->sky.enabled)
		return 0;

	if (!mh_software_scene_prepare(software, matrix, models, NULL, NULL,
		software->cue_enabled ? &software->cue : NULL, &span_count))
		return 0;

	return draw_scene(software, span_count, 0, software->motion_blur, ui);
}

static enum mh_error_code
draw_panorama(struct mh_software_renderer *software,
	const struct mh_projection *projection, const float view[16],
	struct mh_software_sky_state *state, struct mh_error *error_out)
{
	struct mh_software_sky_camera camera;
	unsigned int axis;

	camera.focal = projection->clip.focal;
	camera.horizon_offset = software->sky.horizon;

	for (axis = 0; axis < 3; axis++)
		camera.forward[axis] = -view[axis * 4 + 2];

	/*
	 * Preserve original untouched rows without publishing an incomplete
	 * frame.
	 */
	memcpy(software->work.pixels,
		software->motion_blur
			? software->history[software->history_next]
			: software->surface.pixels,
		software->surface.size);

	return mh_software_sky_draw(&software->work,
		software->sky.image.pixels != NULL ? &software->sky.image
						: NULL,
		&camera, state,
		software->motion_blur ? MH_SOFTWARE_SKY_HISTORY
				: MH_SOFTWARE_SKY_DIRECT,
		software->sky.above, software->sky.below, software->sky.clear,
		error_out);
}

enum mh_error_code
mh_software_renderer_scene_frame_lens(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro,
	const struct mh_renderer_effects *effects, struct mh_error *error_out)
{
	struct mh_software_renderer *software;
	struct mh_software_sky_state sky_state;
	struct mh_depth_cue cue;
	struct mh_software_skid_view skid_view;
	const struct mh_renderer_skids *skids;
	const struct mh_renderer_particles *particles;
	float previous_top;
	float camera[12];
	size_t span_count;
	enum mh_error_code status;

	software = (struct mh_software_renderer *)renderer;

	if (reflections || retro != NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"software lens frames do not yet support reflection passes or retro overlays");

	status = mh_software_ui_check(
		&software->ui, &software->work, ui, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_software_projection_check(projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (projection->clip.extent[0] != software->surface.width ||
		projection->clip.extent[1] != software->surface.height ||
		!mh_software_camera_transform(view, NULL, camera))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"software lens frame viewport or view is invalid");

	sky_state = software->sky.state;
	cue = software->cue;
	previous_top = software->sky.gradient.top;
	software->skids.count = 0;
	software->particles.count = 0;
	software->halos.count = 0;
	software->names.count = 0;
	skids = effects == NULL ? NULL : effects->skids;
	particles = effects == NULL ? NULL : effects->particles;

	if (software->sky.enabled) {
		if (!software->motion_blur) {
			status = draw_panorama(software, projection, view,
				&sky_state, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			software->sky.gradient.top = sky_state.top;
		}

		if (software->sky.height_cue)
			cue.gradient = &software->sky.gradient;
	}

	if (skids != NULL) {
		memset(&skid_view, 0, sizeof(skid_view));
		skid_view.projection = projection;
		skid_view.camera = camera;
		skid_view.world_view = skids->view;
		skid_view.world = skids->world;
		skid_view.lighting = skids->lighting;
		skid_view.cue = software->cue_enabled ? &cue : NULL;
		status = mh_software_skids_prepare(&software->skids,
			skids->histories, skids->cars, &skid_view, error_out);
	}

	if (status == MH_ERROR_NONE && particles != NULL)
		status = mh_software_particles_prepare(&software->particles,
			particles->states, particles->cars, particles->assets,
			projection, camera, particles->frustum,
			particles->brightness, particles->smoke,
			particles->sparks, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_software_halos_prepare(&software->halos,
			effects == NULL ? NULL : effects->halos, projection,
			view, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_software_names_prepare(&software->names,
			effects == NULL ? NULL : effects->names, projection,
			view, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_software_scene_reserve_effects(software, error_out);

	if (status == MH_ERROR_NONE &&
		!mh_software_scene_prepare(software, NULL, models, projection,
			view, software->cue_enabled ? &cue : NULL, &span_count))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"software lens scene preparation failed");

	/* History sky advances the fog top only AFTER face preparation. */
	if (status == MH_ERROR_NONE && software->sky.enabled &&
		software->motion_blur)
		status = draw_panorama(
			software, projection, view, &sky_state, error_out);

	if (status == MH_ERROR_NONE &&
		!draw_scene(software, span_count, software->sky.enabled,
			software->motion_blur, ui))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"software lens scene drawing failed");

	if (status != MH_ERROR_NONE) {
		software->sky.gradient.top = previous_top;
		return status;
	}

	software->sky.state = sky_state;

	if (software->sky.enabled)
		software->sky.gradient.top = sky_state.top;

	if (skids != NULL && skids->oldest_visible != NULL)
		memcpy(skids->oldest_visible, software->skids.oldest_visible,
			skids->cars * sizeof(*skids->oldest_visible));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
