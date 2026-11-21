#define _POSIX_C_SOURCE 200112L
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "backends/network/posix_provider.h"
#include "interfaces/network/resolver_backend.h"

#define LOOPBACK 0x7f000001UL
static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "native resolver line %d: %s\n",       \
				__LINE__, #e);                                 \
			failures++;                                            \
		}                                                              \
	} while (0)

static pthread_mutex_t worker_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_changed = PTHREAD_COND_INITIALIZER;
static pthread_key_t worker_completion;
static int entered, released, finished, fail_lookup;
static char observed_host[64];

int	__real_getaddrinfo(const char *host, const char *service,
	const struct addrinfo *hints, struct addrinfo **addresses);
int	__wrap_getaddrinfo(const char *host, const char *service,
	const struct addrinfo *hints, struct addrinfo **addresses);

/* Gate the OS call, not provider operations: close must return while held. */
int
__wrap_getaddrinfo(const char *host, const char *service,
	const struct addrinfo *hints, struct addrinfo **addresses)
{
	int fail;

	pthread_setspecific(worker_completion, &worker_gate);
	pthread_mutex_lock(&worker_gate);
	strcpy(observed_host, host);
	entered = 1;
	pthread_cond_broadcast(&worker_changed);

	while (!released)
		pthread_cond_wait(&worker_changed, &worker_gate);

	fail = fail_lookup;
	pthread_mutex_unlock(&worker_gate);

	return fail
		? EAI_NONAME
		: __real_getaddrinfo("127.0.0.1", service, hints, addresses);
}

/* Runs after the worker released its request reference. */
static void
worker_finished(void *context)
{
	(void)context;
	pthread_mutex_lock(&worker_gate);
	finished = 1;
	pthread_cond_broadcast(&worker_changed);
	pthread_mutex_unlock(&worker_gate);
}

static void
release_worker(void)
{
	pthread_mutex_lock(&worker_gate);
	released = 1;
	pthread_cond_broadcast(&worker_changed);

	while (!finished)
		pthread_cond_wait(&worker_changed, &worker_gate);

	pthread_mutex_unlock(&worker_gate);
}

static void
check_lookup(int cancel, int fail)
{
	struct mh_resolver *lookup;
	char host[64];
	unsigned long address;
	int done;
	enum mh_error_code status;

	entered = released = finished = 0;
	fail_lookup = fail;
	strcpy(host, "pending.example");
	lookup = NULL;
	status = mh_resolver_open(
		mh_posix_resolver_provider(), host, &lookup, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return;

	strcpy(host, "caller storage changed");
	pthread_mutex_lock(&worker_gate);

	while (!entered)
		pthread_cond_wait(&worker_changed, &worker_gate);

	CHECK(strcmp(observed_host, "pending.example") == 0);
	pthread_mutex_unlock(&worker_gate);
	address = 99;
	done = 42;
	CHECK(mh_resolver_poll(lookup, &done, &address, NULL) == MH_ERROR_NONE);
	CHECK(done == 0 && address == 99);

	if (cancel) {
		mh_resolver_close(lookup);
		lookup = NULL;
		CHECK(!finished);
	}

	release_worker();

	if (lookup != NULL) {
		status = mh_resolver_poll(lookup, &done, &address, NULL);
		CHECK(status == (fail ? MH_ERROR_NOT_FOUND : MH_ERROR_NONE));
		CHECK(done == (fail ? 0 : 1));
		CHECK(address == (fail ? 99 : LOOPBACK));
		CHECK(mh_resolver_poll(lookup, &done, &address, NULL) ==
			status);
		mh_resolver_close(lookup);
	}
}

int
main(void)
{
	struct mh_resolver *lookup;
	unsigned long address;
	int done;

	CHECK(pthread_key_create(&worker_completion, worker_finished) == 0);
	lookup = NULL;
	CHECK(mh_resolver_open(mh_posix_resolver_provider(), "127.0.0.1",
		&lookup, NULL) == MH_ERROR_NONE);
	CHECK(mh_resolver_poll(lookup, &done, &address, NULL) == MH_ERROR_NONE);
	CHECK(done && address == LOOPBACK && !entered);
	mh_resolver_close(lookup);
	check_lookup(0, 0);
	check_lookup(0, 1);
	check_lookup(1, 0);
	pthread_key_delete(worker_completion);
	pthread_cond_destroy(&worker_changed);
	pthread_mutex_destroy(&worker_gate);

	return failures != 0;
}
