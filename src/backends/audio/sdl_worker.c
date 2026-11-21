#include <stdio.h>
#include <stdlib.h>

#include "backends/audio/sdl_worker.h"
#include "engine/core/limits.h"
#include "interfaces/audio/device.h"

struct mh_sdl_audio_worker {
	SDL_AudioStream	*stream;
	SDL_Mutex	*control_mutex;
	SDL_Condition	*wake_condition;
	SDL_Thread	*thread;
	int		(*service)(void *);
	void		*context;
	Uint64		deadline_ms;
	unsigned long	service_calls;
	int		stop_requested, failed;
};

static int
service_stream(struct mh_sdl_audio_worker *worker)
{
	int ok;

	ok = SDL_LockAudioStream(worker->stream);

	if (ok) {
		ok = worker->service(worker->context);

		if (!SDL_UnlockAudioStream(worker->stream))
			ok = 0;
	} else
		fprintf(stderr, "Audio worker: %s\n", SDL_GetError());

	return ok;
}

static int SDLCALL
run_worker(void *context)
{
	struct mh_sdl_audio_worker *worker;
	Uint64 now;
	int ok;

	worker = (struct mh_sdl_audio_worker *)context;
	SDL_LockMutex(worker->control_mutex);

	while (!worker->stop_requested && !worker->failed) {
		now = SDL_GetTicks();

		if (now < worker->deadline_ms) {
			SDL_WaitConditionTimeout(worker->wake_condition,
				worker->control_mutex,
				(Sint32)(worker->deadline_ms - now));
			continue;
		}

		/*
		 * Never wait for the stream while holding the control mutex:
		 * inspectors may hold the stream and request counters/status.
		 */
		SDL_UnlockMutex(worker->control_mutex);
		ok = service_stream(worker);

		SDL_LockMutex(worker->control_mutex);
		worker->service_calls =
			(worker->service_calls + 1) & MH_U32_MAX;
		worker->failed = !ok;
		worker->deadline_ms += MH_AUDIO_SERVICE_MS;
		now = SDL_GetTicks();

		/*
		 * Native late-wakeup policy: one service pass, no burst of
		 * missed callbacks. Ordinary callbacks stay on their 40 ms
		 * deadlines.
		 */
		if (worker->deadline_ms <= now)
			worker->deadline_ms = now + MH_AUDIO_SERVICE_MS;
	}

	SDL_UnlockMutex(worker->control_mutex);

	return 0;
}

struct mh_sdl_audio_worker *
mh_sdl_audio_worker_create(
	SDL_AudioStream *stream, int (*service)(void *), void *context)
{
	struct mh_sdl_audio_worker *worker;

	if (stream == NULL || service == NULL) {
		SDL_SetError("Missing audio worker stream or service");
		return NULL;
	}

	worker = (struct mh_sdl_audio_worker *)calloc(1, sizeof(*worker));

	if (worker == NULL) {
		SDL_SetError("Cannot allocate audio worker");
		return NULL;
	}

	worker->stream = stream;
	worker->service = service;
	worker->context = context;
	worker->control_mutex = SDL_CreateMutex();

	if (worker->control_mutex != NULL)
		worker->wake_condition = SDL_CreateCondition();

	if (worker->wake_condition != NULL) {
		worker->deadline_ms = SDL_GetTicks() + MH_AUDIO_SERVICE_MS;
		worker->thread = SDL_CreateThread(
			run_worker, "Motorhead effects", worker);
	}

	if (worker->thread == NULL) {
		mh_sdl_audio_worker_destroy(worker);
		return NULL;
	}

	return worker;
}

void
mh_sdl_audio_worker_destroy(struct mh_sdl_audio_worker *worker)
{
	if (worker == NULL)
		return;

	if (worker->thread != NULL) {
		SDL_LockMutex(worker->control_mutex);
		worker->stop_requested = 1;
		SDL_SignalCondition(worker->wake_condition);
		SDL_UnlockMutex(worker->control_mutex);
		SDL_WaitThread(worker->thread, NULL);
	}

	if (worker->wake_condition != NULL)
		SDL_DestroyCondition(worker->wake_condition);

	if (worker->control_mutex != NULL)
		SDL_DestroyMutex(worker->control_mutex);

	free(worker);
}

int
mh_sdl_audio_worker_ok(struct mh_sdl_audio_worker *worker)
{
	int ok;

	if (worker == NULL)
		return 1;

	SDL_LockMutex(worker->control_mutex);
	ok = !worker->failed;
	SDL_UnlockMutex(worker->control_mutex);

	return ok;
}

unsigned long
mh_sdl_audio_worker_calls(struct mh_sdl_audio_worker *worker)
{
	unsigned long service_calls;

	if (worker == NULL)
		return 0;

	SDL_LockMutex(worker->control_mutex);
	service_calls = worker->service_calls;
	SDL_UnlockMutex(worker->control_mutex);

	return service_calls;
}
