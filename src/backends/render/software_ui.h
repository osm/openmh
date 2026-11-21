#ifndef MH_SOFTWARE_UI_H
#define MH_SOFTWARE_UI_H

#include "backends/render/software_raster.h"
#include "engine/ui/draw.h"

/*
 * Zero initialize before upload. Owns the copied immutable atlas; native UI
 * triangles use nearest texels and RGBA tint.
 */
struct mh_software_ui {
	struct mh_image	atlas;
};

void			mh_software_ui_free(struct mh_software_ui *ui);
enum mh_error_code	mh_software_ui_upload(struct mh_software_ui *ui,
	const struct mh_ui_atlas *atlas, struct mh_error *error_out);

/* Validate the complete list before changing any pixels. NULL is no overlay. */
enum mh_error_code	mh_software_ui_check(const struct mh_software_ui *ui,
	const struct mh_software_surface *surface,
	const struct mh_ui_list *list, struct mh_error *error_out);

/* Requires the same successfully checked inputs, unchanged during drawing. */
void	mh_software_ui_draw(const struct mh_software_ui *ui,
	const struct mh_software_surface *surface,
	const struct mh_ui_list *list);

#endif
