#ifndef MH_AUDIO_DEVICE_H
#define MH_AUDIO_DEVICE_H

#include <stddef.h>

#include "engine/core/error.h"

#define MH_AUDIO_OUTPUT_RATE 48000
#define MH_AUDIO_OUTPUT_CHANNELS 2

#define MH_AUDIO_OUTPUT_CHUNK 512
#define MH_AUDIO_OUTPUT_REQUEST_MAX 8192

#define MH_AUDIO_SERVICE_MS 40

struct mh_audio_provider;
struct mh_audio_device;

enum mh_audio_encoding {
	MH_AUDIO_NATIVE_FLOAT,
	MH_AUDIO_S16LE
};

struct mh_audio_format {
	enum mh_audio_encoding	encoding;
	unsigned int		rate, channels;
};

struct mh_audio_callbacks {
	/* Optional demand callback, with 1..REQUEST_MAX source frames. */
	void		(*request)(void *context, size_t frames);

	/* Periodic effects service, independent of game FPS. */
	enum mh_error_code (*service)(
		void	*context, struct mh_error *error_out);

	void		*context;
};

struct mh_audio_device_state {
	unsigned long	service_calls;
	int		failed;
};

/*
 * Open the game mixer's 48000 Hz stereo native-float device, paused.
 * The descriptor is borrowed through close; callbacks
 * are copied and their context is borrowed through close. Open does not call
 * them. NULL provider is unavailable, and failures leave output unchanged.
 */
enum mh_error_code	mh_audio_device_open(
	const struct mh_audio_provider *provider,
	const struct mh_audio_callbacks *callbacks,
	struct mh_audio_device **output, struct mh_error *error_out);

/*
 * Open a paused device with explicit input format. Format and callbacks are
 * copied during open. NULL callbacks selects queued PCM with no demand or
 * periodic service. Native output conversion belongs to the provider; source
 * bytes and rate are not converted by dispatch. Unsupported formats fail.
 */
enum mh_error_code	mh_audio_device_open_format(
	const struct mh_audio_provider *provider,
	const struct mh_audio_format *format,
	const struct mh_audio_callbacks *callbacks,
	struct mh_audio_device **output, struct mh_error *error_out);

/* Start PCM consumption and registered callbacks; once, outside exclusion. */
enum mh_error_code	mh_audio_device_start(struct mh_audio_device *device,
	struct mh_error *error_out);

/*
 * Serializes owner edits with both callbacks. Native providers may use a lock;
 * a host-serialized provider needs no thread or mutex. Callbacks already own
 * this exclusion and must not lock again. Do not close/start while holding it.
 */
enum mh_error_code	mh_audio_device_lock(struct mh_audio_device *device,
	struct mh_error *error_out);
enum mh_error_code	mh_audio_device_unlock(struct mh_audio_device *device,
	struct mh_error *error_out);

/* Under exclusion: discard queued PCM, or copy interleaved native float PCM. */
enum mh_error_code	mh_audio_device_clear(struct mh_audio_device *device,
	struct mh_error *error_out);
enum mh_error_code	mh_audio_device_submit(struct mh_audio_device *device,
	const float *samples, size_t frames, struct mh_error *error_out);

/*
 * Copy complete interleaved source frames in the opened format. Bytes are
 * borrowed for the call only. Under callback exclusion, or from the sole
 * producer when no callbacks were registered. No conversion or frame limit
 * beyond addressable byte storage is imposed here; a provider may reject a
 * write exceeding its capacity. The float submit convenience above retains
 * the game mixer's format and REQUEST_MAX bounds.
 */
enum mh_error_code	mh_audio_device_write(struct mh_audio_device *device,
	const void *bytes, size_t size, struct mh_error *error_out);

/*
 * Optional finite-stream operations, under the same producer exclusion.
 * Flush exposes buffered conversion tails; it neither clears nor waits.
 * After flush, drained reports whether all available converted output has
 * been consumed. It is not the unconverted input queue depth and does not
 * promise the final sample has reached a physical speaker. Poll without
 * blocking; absent operations return UNSUPPORTED, leaving outputs unchanged.
 */
enum mh_error_code	mh_audio_device_flush(struct mh_audio_device *device,
	struct mh_error *error_out);
enum mh_error_code	mh_audio_device_drained(struct mh_audio_device *device,
	int *drained, struct mh_error *error_out);

/* Under exclusion; no service or output advancement as a side effect. */
enum mh_error_code	mh_audio_device_inspect(struct mh_audio_device *device,
	struct mh_audio_device_state *state, struct mh_error *error_out);

/*
 * Outside exclusion: pause/resume PCM demand without changing the queue or
 * periodic service. Pause returns only after in-flight demand is quiescent.
 */
enum mh_error_code	mh_audio_device_pause(struct mh_audio_device *device,
	int paused, struct mh_error *error_out);

/*
 * Outside exclusion: detach/join periodic service only. Idempotent; subsequent
 * inspection reports zero service calls and no service failure. PCM continues.
 */
enum mh_error_code	mh_audio_device_stop_service(
	struct mh_audio_device *device, struct mh_error *error_out);

/*
 * Under exclusion: replace demand callback, preserving context and queued PCM.
 */
enum mh_error_code	mh_audio_device_set_request(
	struct mh_audio_device *device, void (*request)(void *, size_t),
	struct mh_error *error_out);

/*
 * Optional diagnostic queue query, under exclusion; unavailable returns
 * UNSUPPORTED.
 */
enum mh_error_code	mh_audio_device_queued(struct mh_audio_device *device,
	size_t *frames, struct mh_error *error_out);

/* Stops/detaches both callbacks before returning; NULL is a no-op. */
void	mh_audio_device_close(struct mh_audio_device *device);

#endif
