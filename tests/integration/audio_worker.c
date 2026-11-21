#include <stdio.h>
#include <string.h>

#include "backends/audio/sdl_worker.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	SDL_Semaphore	*entered;
	Uint64		times[8];
	unsigned int	calls, fail_at, delay;
};

static int
service(void *context)
{
	struct fixture *fixture;

	fixture = (struct fixture *)context;

	if (fixture->calls < 8)
		fixture->times[fixture->calls] = SDL_GetTicks();

	fixture->calls++;
	SDL_SignalSemaphore(fixture->entered);

	if (fixture->delay != 0)
		SDL_Delay(fixture->delay);

	return fixture->fail_at == 0 || fixture->calls != fixture->fail_at;
}

int
main(void)
{
	struct fixture fixture;
	struct mh_sdl_audio_worker *worker;
	SDL_AudioStream *stream;
	SDL_AudioSpec spec;
	Uint64 started, limit;
	unsigned int i, saved;

	CHECK(SDL_InitSubSystem(SDL_INIT_AUDIO));
	spec.freq = 48000;
	spec.channels = 2;
	spec.format = SDL_AUDIO_F32;
	stream = SDL_CreateAudioStream(&spec, &spec);
	CHECK(stream != NULL);
	memset(&fixture, 0, sizeof(fixture));
	fixture.entered = SDL_CreateSemaphore(0);
	CHECK(fixture.entered != NULL);
	CHECK(mh_sdl_audio_worker_create(NULL, service, &fixture) == NULL);
	CHECK(mh_sdl_audio_worker_create(stream, NULL, &fixture) == NULL);
	mh_sdl_audio_worker_destroy(NULL);

	/*
	 * A producer's stream lock excludes the independent consumer. No
	 * renderer/update call is used to wake it after releasing that lock.
	 */
	CHECK(SDL_LockAudioStream(stream));
	started = SDL_GetTicks();
	worker = mh_sdl_audio_worker_create(stream, service, &fixture);
	CHECK(worker != NULL);
	SDL_Delay(90);
	CHECK(fixture.calls == 0);
	CHECK(SDL_UnlockAudioStream(stream));

	for (i = 0; i < 3; i++)
		CHECK(SDL_WaitSemaphoreTimeout(fixture.entered, 2000));

	CHECK(SDL_LockAudioStream(stream));
	CHECK(fixture.calls >= 3 && fixture.times[0] >= started + 40);
	CHECK(fixture.times[2] - fixture.times[0] >= 70);
	saved = fixture.calls;
	SDL_Delay(100);
	CHECK(fixture.calls == saved);
	CHECK(mh_sdl_audio_worker_ok(worker));
	CHECK(SDL_UnlockAudioStream(stream));
	mh_sdl_audio_worker_destroy(worker);

	/* A failing service is reported and never re-entered. */
	SDL_DestroySemaphore(fixture.entered);
	memset(&fixture, 0, sizeof(fixture));
	fixture.entered = SDL_CreateSemaphore(0);
	CHECK(fixture.entered != NULL);
	fixture.fail_at = 1;
	worker = mh_sdl_audio_worker_create(stream, service, &fixture);
	CHECK(worker != NULL);
	CHECK(SDL_WaitSemaphoreTimeout(fixture.entered, 2000));
	limit = SDL_GetTicks() + 2000;

	while (mh_sdl_audio_worker_ok(worker) && SDL_GetTicks() < limit)
		SDL_Delay(1);

	CHECK(!mh_sdl_audio_worker_ok(worker) &&
		mh_sdl_audio_worker_calls(worker) == 1);
	SDL_Delay(100);
	mh_sdl_audio_worker_destroy(worker);
	CHECK(fixture.calls == 1);

	/*
	 * Closing during a callback joins it before the context can be freed.
	 */
	fixture.calls = 0;
	fixture.fail_at = 0;
	fixture.delay = 80;
	worker = mh_sdl_audio_worker_create(stream, service, &fixture);
	CHECK(worker != NULL);
	CHECK(SDL_WaitSemaphoreTimeout(fixture.entered, 2000));
	mh_sdl_audio_worker_destroy(worker);
	CHECK(fixture.calls == 1);
	fixture.delay = 0;

	for (i = 0; i < 16; i++) {
		worker = mh_sdl_audio_worker_create(stream, service, &fixture);
		CHECK(worker != NULL);
		mh_sdl_audio_worker_destroy(worker);
	}

	SDL_DestroySemaphore(fixture.entered);
	SDL_DestroyAudioStream(stream);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	puts("Independent effects cadence, producer exclusion, failure and joined teardown passed");

	return 0;
}
