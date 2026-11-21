#ifndef MH_AUDIO_BACKEND_H
#define MH_AUDIO_BACKEND_H

#include "interfaces/audio/device.h"

/* Descriptor, table and context are borrowed unchanged through device close. */
struct mh_audio_provider {
	const struct mh_audio_device_ops	*ops;
	void					*context;
};

/*
 * One token per device; no process-global service selection. Open cleans its
 * partial state on failure, leaving output unchanged. Successful open receives
 * exactly one close, even if start fails. Copy format/callback values in open;
 * those argument structures are borrowed only during the call. Diagnostics
 * survive until the next operation on the provider/device. Copy them before
 * cleanup when retaining.
 *
 * Start enables serialized request/service callbacks, never on game updates.
 * Service uses 40 ms deadlines, one pass after a late wakeup without a catch-up
 * burst. Providers own scheduling, output and callback exclusion together.
 * A host-driven provider can deliver callbacks from its event/audio scheduler;
 * it need not emulate native threads, sleeps or locks. No callback may access
 * the borrowed context after close, including delayed platform completions.
 *
 * Start may report unavailable (for example, audio activation not permitted).
 * This contract does not implement a browser user-gesture activation flow.
 */
struct mh_audio_device_ops {
	enum mh_error_code (*open)(void *context,
		const struct mh_audio_format *format,
		const struct mh_audio_callbacks *callbacks, void **output,
		struct mh_error	*error_out);
	void			(*close)(void *context, void *device);
	enum mh_error_code (*start)(
		void	*context, void *device, struct mh_error *error_out);

	enum mh_error_code (*lock)(
		void	*context, void *device, struct mh_error *error_out);
	enum mh_error_code (*unlock)(
		void	*context, void *device, struct mh_error *error_out);

	enum mh_error_code (*clear)(
		void	*context, void *device, struct mh_error *error_out);
	enum mh_error_code (*write)(void *context, void *device,
		const void	*bytes, size_t size, struct mh_error *error_out);

	enum mh_error_code (*inspect)(void *context, void *device,
		struct mh_audio_device_state *state,
		struct mh_error	*error_out);

	enum mh_error_code (*pause)(void *context, void *device, int paused,
		struct mh_error	*error_out);
	enum mh_error_code (*stop_service)(
		void	*context, void *device, struct mh_error *error_out);

	/* Under callback exclusion; failure retains the previous request. */
	enum mh_error_code (*set_request)(void *context, void *device,
		void	(*request)(void *, size_t), struct mh_error *error_out);

	/* Optional; only the explicit offline fixture uses queue depth. */
	enum mh_error_code (*queued)(void *context, void *device,
		size_t		*frames, struct mh_error *error_out);

	/* Optional finite-stream tail exposure and nonblocking drain query. */
	enum mh_error_code (*flush)(
		void	*context, void *device, struct mh_error *error_out);
	enum mh_error_code (*drained)(void *context, void *device, int *drained,
		struct mh_error	*error_out);
};

enum mh_error_code	mh_audio_provider_init(
	struct mh_audio_provider *provider,
	const struct mh_audio_device_ops *ops, void *context,
	struct mh_error *error_out);
#endif
