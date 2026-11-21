#include "interfaces/render/capture.h"

enum mh_error_code
mh_capture_image(const struct mh_image_capture *capture,
	const struct mh_image *image, const char *path,
	struct mh_error *error_out)
{
	if (capture == NULL || capture->save == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"diagnostic image capture is unavailable");

	return capture->save(capture->context, image, path, error_out);
}
