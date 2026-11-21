#include <stdlib.h>
#include <string.h>

#include "engine/assets/scene.h"
#include "engine/assets/tbl.h"

enum mh_error_code
mh_scene_lws_decode(const unsigned char *data, size_t size,
	struct mh_scene *scene, struct mh_error *error_out)
{
	unsigned char *text;
	size_t text_size;
	enum mh_error_code status;

	if (data == NULL || scene == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing scene input/output");

	if (size < 3 || memcmp(data, "TBL", 3) != 0)
		return mh_scene_lws_parse(data, size, scene, error_out);

	status = mh_tbl_decode(data, size, &text, &text_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_scene_lws_parse(text, text_size, scene, error_out);
	free(text);

	return status;
}

enum mh_error_code
mh_motion_mot_decode(const unsigned char *data, size_t size,
	struct mh_scene_object *motion, struct mh_error *error_out)
{
	unsigned char *text;
	size_t text_size;
	enum mh_error_code status;

	if (data == NULL || motion == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing motion input/output");

	if (size < 3 || memcmp(data, "TBL", 3) != 0)
		return mh_motion_mot_parse(data, size, motion, error_out);

	status = mh_tbl_decode(data, size, &text, &text_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_motion_mot_parse(text, text_size, motion, error_out);
	free(text);

	return status;
}
