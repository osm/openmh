#ifndef MH_RENDER_CAPTURE_H
#define MH_RENDER_CAPTURE_H

#include "engine/assets/image.h"

/*
 * Optional diagnostic image sink. Descriptor/context are borrowed through use.
 * Image and path are borrowed only during save; failure diagnostics follow the
 * provider's lifetime contract. This is independent of in-game picture saves.
 */
struct mh_image_capture {
	void			*context;
	enum mh_error_code (*save)(void *context, const struct mh_image *image,
		const char	*path, struct mh_error *error_out);
};

enum mh_error_code	mh_capture_image(const struct mh_image_capture *capture,
	const struct mh_image *image, const char *path,
	struct mh_error *error_out);

#endif
