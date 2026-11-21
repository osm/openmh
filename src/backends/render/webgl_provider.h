#ifndef MH_WEBGL_PROVIDER_H
#define MH_WEBGL_PROVIDER_H

#include "interfaces/render/canvas.h"

struct mh_render_backend;

/* Browser canvas drawing shares the window's context with WebGL races. */
const struct mh_canvas_provider	*mh_webgl_canvas_provider(void);

/* Scene providers require an SDL browser window, retained until destruction. */
const struct mh_render_backend	*mh_webgl_glide_backend(void);
const struct mh_render_backend	*mh_webgl_ui_backend(void);
const struct mh_render_backend	*mh_webgl_direct3d_backend(void);
const struct mh_render_backend	*mh_webgl_powervr_backend(void);

/* Restore active presentation owners before the host resumes game time. */
enum mh_error_code	mh_webgl_restore_windows(struct mh_error *error_out);

/* Also catches the interval before the browser delivers its loss event. */
int	mh_webgl_presentation_lost(void);

const struct mh_render_backend	*mh_webgl_openmh_backend(void);

#endif
