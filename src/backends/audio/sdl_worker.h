#ifndef MH_SDL_AUDIO_WORKER_H
#define MH_SDL_AUDIO_WORKER_H

#include <SDL3/SDL.h>

/*
 * SDL-private periodic service with a 40 ms effects callback. Each callback
 * owns the stream lock, shared with producers and inspectors. It must not
 * lock/close the audio device itself.
 */
/* The stream and callback context are borrowed until destroy returns. */
struct mh_sdl_audio_worker;
struct mh_sdl_audio_worker	*mh_sdl_audio_worker_create(
	SDL_AudioStream *stream, int (*service)(void *), void *context);
#ifdef __EMSCRIPTEN__
/*
 * Browser demand advances the 40 ms service clock in source samples. Split
 * large requests at service boundaries so delayed event-loop callbacks cannot
 * skip a ring refill. The request caller already holds the SDL stream lock.
 * NULL worker forwards the demand unchanged (music/movie-only streams).
 */
struct mh_sdl_audio_worker	*mh_sdl_audio_worker_create_clocked(
	SDL_AudioStream *stream, int (*service)(void *), void *context,
	unsigned int rate);
void				 mh_sdl_audio_worker_request(
	struct mh_sdl_audio_worker *worker, size_t frames,
	void (*request)(void *, size_t), void *context);
#endif
/*
 * Detach the browser timer or join the native worker before destroying the
 * stream or borrowed context. The caller must not hold the stream lock while
 * waiting. NULL is a no-op.
 */
void		mh_sdl_audio_worker_destroy(struct mh_sdl_audio_worker *worker);
int		mh_sdl_audio_worker_ok(struct mh_sdl_audio_worker *worker);
unsigned long	mh_sdl_audio_worker_calls(struct mh_sdl_audio_worker *worker);

#endif
