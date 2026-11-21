#ifndef MH_CLIENT_DIAGNOSTICS_MODEL_VIEWER_H
#define MH_CLIENT_DIAGNOSTICS_MODEL_VIEWER_H

#include "engine/render/mesh.h"

/*
 * NULL capture path runs interactively. Otherwise render one frame, save a
 * BMP and exit. Headless requires a capture path and creates no window.
 * Returns zero on failure and reports the platform error.
 */
struct mh_model_viewer_options {
	const char	*capture_path;
	int		headless;
	int		reflections;

	/* Orbit angles in radians. */
	double		yaw;
	double		pitch;

	/* Initial time; captures use this exact value. */
	double		animation_seconds;
	int		paused;
};

int	mh_model_viewer_run(struct mh_mesh *mesh, const char *title,
	const struct mh_model_viewer_options *options);

#endif
