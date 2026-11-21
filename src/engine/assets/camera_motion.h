#ifndef MH_ASSETS_CAMERA_MOTION_H
#define MH_ASSETS_CAMERA_MOTION_H

#include "engine/core/error.h"
#include "engine/navigation/camera_motion.h"

/*
 * Output owns its keys on success; untouched on error. Free an old output
 * before replacing it. The input is decoded/plain LWMO version 1 text.
 */
enum mh_error_code	mh_camera_mot_parse(const unsigned char *text,
	size_t size, struct mh_camera_motion *motion,
	struct mh_error *error_out);

#endif
