#ifndef MH_VIDEO_PLAYBACK_H
#define MH_VIDEO_PLAYBACK_H

#include "interfaces/video/backend.h"

struct mh_audio_provider;
struct mh_video_source_provider;
struct mh_video_decoder_provider;
struct mh_window_system;
struct mh_canvas_provider;
struct mh_input_provider;
struct mh_clock_source;
struct mh_image;

/*
 * Selected clip source, decoder and presentation services for portable
 * playback. Borrow this configuration and every descriptor unchanged until all
 * its players close. Input drains the active screen's shared event stream
 * without device polling. Clock pacing is host-driven: no delay operation is
 * needed. Capture is an optional diagnostic sink; image/path are borrowed only
 * during its call. NULL audio permits silent playback. Finite-stream output
 * requires the audio provider's S16LE format, flush and drain support.
 */
struct mh_video_services {
	const struct mh_video_source_provider	*clips;
	const struct mh_video_decoder_provider	*decoder;
	const struct mh_window_system		*windows;
	const struct mh_canvas_provider		*canvases;
	const struct mh_input_provider		*inputs;
	const struct mh_clock_source		*clock;
	const struct mh_audio_provider		*audio;
	enum mh_error_code (*capture)(void *context,
		const struct mh_image *image, const char *path,
		struct mh_error			*error_out);
	void					*context;
};

/*
 * Construct a playback provider using the supplied decoder/presentation
 * services. The implementation owns its private state. Failure preserves
 * output. Close every player before closing its provider. Services remain
 * borrowed. Close accepts only providers returned here, or NULL.
 */
enum mh_error_code	mh_video_playback_open(
	const struct mh_video_services *services,
	const struct mh_video_provider **output, struct mh_error *error_out);
void			mh_video_playback_close(
	const struct mh_video_provider *provider);

#endif
