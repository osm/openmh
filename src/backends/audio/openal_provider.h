#ifndef MH_OPENAL_AUDIO_PROVIDER_H
#define MH_OPENAL_AUDIO_PROVIDER_H

#include "interfaces/audio/backend.h"

/*
 * Emscripten's bundled OpenAL queues mixed PCM for Web Audio playback.
 * One browser event-loop owner; close devices before their provider.
 * Suspension preserves queues and each device's explicit pause state.
 */
enum mh_error_code	mh_openal_audio_open(
	const struct mh_audio_provider **output, struct mh_error *error_out);
enum mh_error_code	mh_openal_audio_suspend(
	const struct mh_audio_provider *provider, int suspended,
	struct mh_error *error_out);
void			mh_openal_audio_close(
	const struct mh_audio_provider *provider);

/* Refill on browser frames too: timer tasks can be delayed while RAF runs. */
void	mh_openal_audio_update(const struct mh_audio_provider *provider);

#endif
