#include <emscripten/eventloop.h>
#include <stdlib.h>

#include "backends/audio/sdl_worker.h"
#include "engine/core/limits.h"
#include "interfaces/audio/device.h"

#ifdef __EMSCRIPTEN_PTHREADS__
#error Browser audio service assumes one event-loop owner
#endif

struct mh_sdl_audio_worker {
	SDL_AudioStream	*stream;
	int		(*service)(void *);
	void		*context;
	int		interval, failed;
	unsigned long	service_calls;
	Uint64		phase, period;
};

static void
service_interval(void *context)
{
	struct mh_sdl_audio_worker *worker;
	int ok;

	worker = context;

	if (worker->failed)
		return;

	ok = SDL_LockAudioStream(worker->stream);

	if (ok) {
		ok = worker->service(worker->context);

		if (!SDL_UnlockAudioStream(worker->stream))
			ok = 0;
	}

	worker->service_calls = (worker->service_calls + 1) & MH_U32_MAX;
	worker->failed = !ok;
}

static struct mh_sdl_audio_worker *
create(SDL_AudioStream *stream, int (*service)(void *), void *context,
	unsigned int rate)
{
	struct mh_sdl_audio_worker *worker;

	if (stream == NULL || service == NULL) {
		SDL_SetError("Missing audio worker stream or service");
		return NULL;
	}

	worker = calloc(1, sizeof(*worker));

	if (worker == NULL) {
		SDL_SetError("Cannot allocate audio worker");
		return NULL;
	}

	worker->stream = stream;
	worker->service = service;
	worker->context = context;
	worker->period = (Uint64)rate * MH_AUDIO_SERVICE_MS;

	/*
	 * Browser callbacks share one event loop with device owners and PCM
	 * demand. A delayed timer services once, never a burst of missed ticks.
	 */
	if (rate == 0)
		worker->interval = emscripten_set_interval(
			service_interval, MH_AUDIO_SERVICE_MS, worker);

	return worker;
}

struct mh_sdl_audio_worker *
mh_sdl_audio_worker_create(
	SDL_AudioStream *stream, int (*service)(void *), void *context)
{
	return create(stream, service, context, 0);
}

struct mh_sdl_audio_worker *
mh_sdl_audio_worker_create_clocked(SDL_AudioStream *stream,
	int (*service)(void *), void *context, unsigned int rate)
{
	if (rate == 0) {
		SDL_SetError("Missing audio service sample rate");
		return NULL;
	}

	return create(stream, service, context, rate);
}

void
mh_sdl_audio_worker_request(struct mh_sdl_audio_worker *worker, size_t frames,
	void (*request)(void *, size_t), void *context)
{
	size_t count;
	Uint64 until;

	if (worker == NULL || worker->period == 0) {
		request(context, frames);
		return;
	}

	while (frames != 0 && !worker->failed) {
		until = (worker->period - worker->phase + 999) / 1000;
		count = until < frames ? (size_t)until : frames;
		request(context, count);
		frames -= count;
		worker->phase += (Uint64)count * 1000;

		while (worker->phase >= worker->period && !worker->failed) {
			worker->phase -= worker->period;
			worker->failed = !worker->service(worker->context);
			worker->service_calls =
				(worker->service_calls + 1) & MH_U32_MAX;
		}
	}
}

void
mh_sdl_audio_worker_destroy(struct mh_sdl_audio_worker *worker)
{
	if (worker == NULL)
		return;

	if (worker->interval != 0)
		emscripten_clear_interval(worker->interval);

	free(worker);
}

int
mh_sdl_audio_worker_ok(struct mh_sdl_audio_worker *worker)
{
	return worker == NULL || !worker->failed;
}

unsigned long
mh_sdl_audio_worker_calls(struct mh_sdl_audio_worker *worker)
{
	return worker != NULL ? worker->service_calls : 0;
}
