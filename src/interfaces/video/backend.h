#ifndef MH_VIDEO_BACKEND_H
#define MH_VIDEO_BACKEND_H

#include "interfaces/video/video.h"

/* Descriptor, table and context are borrowed unchanged through player close. */
struct mh_video_provider {
	const struct mh_video_ops	*ops;
	void				*context;
};

struct mh_video_ops {
	/*
	 * Do not wait for external readiness; defer that work to step. Options
	 * and their borrowed pointers remain valid through close. Failed open
	 * cleans partial state and leaves output unchanged; successful open
	 * receives one close.
	 */
	enum mh_error_code (*open)(void *context,
		const struct mh_video_options *options, void **output,
		struct mh_error	*error_out);

	/*
	 * Nonblocking: draw at most one frame or poll readiness/drain. Only
	 * DONE supplies an outcome; delay is a host pacing hint, never a sleep
	 * here. FAILED supplies a diagnostic borrowed until the next operation.
	 */
	enum mh_video_status (*step)(void *context, void *player,
		unsigned long *delay, enum mh_video_outcome *outcome,
		struct mh_error	*error_out);

	/*
	 * Detach pending callbacks before returning, including during loading.
	 */
	void			(*close)(void *context, void *player);

	/* Optional collection lookup; follows mh_video_resolve_from. */
	enum mh_error_code (*resolve)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, char *output, size_t capacity,
		int		*found, struct mh_error *error_out);
};

enum mh_error_code	mh_video_provider_init(
	struct mh_video_provider *provider, const struct mh_video_ops *ops,
	void *context, struct mh_error *error_out);

#endif
