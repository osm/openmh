#ifndef MH_ASSETS_SCENE_H
#define MH_ASSETS_SCENE_H

#include "engine/core/error.h"
#include "interfaces/animation/scene.h"

/*
 * Parses decoded text and owns every object key as well as initial poses.
 * No parenting, pivots or external motion files.
 * Output unchanged on failure. Release successful output with mh_scene_free.
 */
enum mh_error_code	mh_scene_lws_parse(const unsigned char *text,
	size_t size, struct mh_scene *scene, struct mh_error *error_out);

/* Plain LWSC or TBL05-wrapped scene, with the same ownership as parse. */
enum mh_error_code	mh_scene_lws_decode(const unsigned char *data,
	size_t size, struct mh_scene *scene, struct mh_error *error_out);

/*
 * Standalone nine-channel LWMO version 1, including single-key start poses.
 * Time is in raw frames, with the original repeat end behavior. Caller owns
 * output.keys and frees it with free(); output is unchanged on failure.
 * Route consumers impose their separate minimum of three keys.
 */
enum mh_error_code	mh_motion_mot_parse(const unsigned char *text,
	size_t size, struct mh_scene_object *output,
	struct mh_error *error_out);

/* Plain LWMO or TBL05-wrapped motion, with the same ownership as parse. */
enum mh_error_code	mh_motion_mot_decode(const unsigned char *data,
	size_t size, struct mh_scene_object *output,
	struct mh_error *error_out);

#endif
