#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "backends/network/windows_provider.h"

#define LOOPBACK_ADDRESS 0x7f000001UL
#define WORKER_TIMEOUT_MS 5000
#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "Windows resolver line %d: %s\n",      \
				__LINE__, #condition);                         \
			exit(1);                                               \
		}                                                              \
	} while (0)

static HANDLE entered, released, worker;
static int fail_lookup;
static char observed_host[64];

extern	int(WSAAPI *__real___imp_getaddrinfo)(const char *host,
	const char *service, const struct addrinfo *hints,
	struct addrinfo **addresses);

/* Hold the OS call: closing a request must not wait for this worker. */
static int WSAAPI
resolve_gated(const char *host, const char *service,
	const struct addrinfo *hints, struct addrinfo **addresses)
{
	CHECK(strlen(host) < sizeof(observed_host));

	strcpy(observed_host, host);
	worker = OpenThread(SYNCHRONIZE, FALSE, GetCurrentThreadId());
	CHECK(worker != NULL);
	CHECK(SetEvent(entered));
	CHECK(WaitForSingleObject(released, WORKER_TIMEOUT_MS) ==
		WAIT_OBJECT_0);

	return fail_lookup ? EAI_NONAME
			: __real___imp_getaddrinfo(
				"127.0.0.1", service, hints, addresses);
}

/* MinGW emits an indirect call through this imported function pointer. */
int(WSAAPI *__wrap___imp_getaddrinfo)(const char *host, const char *service,
	const struct addrinfo *hints,
	struct addrinfo **addresses) = resolve_gated;

static void
check_lookup(int cancel, int fail)
{
	struct mh_resolver *lookup;
	char host[64];
	unsigned long address;
	int done;
	enum mh_error_code status;

	CHECK(ResetEvent(entered));
	CHECK(ResetEvent(released));
	fail_lookup = fail;
	strcpy(host, "pending.example");
	lookup = NULL;
	CHECK(mh_resolver_open(mh_windows_resolver_provider(), host, &lookup,
		NULL) == MH_ERROR_NONE);
	strcpy(host, "caller storage changed");
	CHECK(WaitForSingleObject(entered, WORKER_TIMEOUT_MS) == WAIT_OBJECT_0);
	CHECK(strcmp(observed_host, "pending.example") == 0);
	address = 99;
	done = 42;
	CHECK(mh_resolver_poll(lookup, &done, &address, NULL) == MH_ERROR_NONE);
	CHECK(done == 0 && address == 99);

	if (cancel) {
		mh_resolver_close(lookup);
		lookup = NULL;
		CHECK(WaitForSingleObject(worker, 0) == WAIT_TIMEOUT);
	}

	CHECK(SetEvent(released));

	/* Thread termination follows publication and release of its request. */
	CHECK(WaitForSingleObject(worker, WORKER_TIMEOUT_MS) == WAIT_OBJECT_0);
	CloseHandle(worker);

	if (lookup != NULL) {
		status = mh_resolver_poll(lookup, &done, &address, NULL);
		CHECK(status == (fail ? MH_ERROR_NOT_FOUND : MH_ERROR_NONE));
		CHECK(done == (fail ? 0 : 1));
		CHECK(address == (fail ? 99 : LOOPBACK_ADDRESS));
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

	entered = CreateEventW(NULL, TRUE, FALSE, NULL);
	released = CreateEventW(NULL, TRUE, FALSE, NULL);
	CHECK(entered != NULL && released != NULL);
	lookup = NULL;
	CHECK(mh_resolver_open(mh_windows_resolver_provider(), "127.0.0.1",
		&lookup, NULL) == MH_ERROR_NONE);
	CHECK(mh_resolver_poll(lookup, &done, &address, NULL) == MH_ERROR_NONE);
	CHECK(done && address == LOOPBACK_ADDRESS);
	CHECK(WaitForSingleObject(entered, 0) == WAIT_TIMEOUT);
	mh_resolver_close(lookup);
	check_lookup(0, 0);
	check_lookup(0, 1);
	check_lookup(1, 0);
	CloseHandle(entered);
	CloseHandle(released);
	puts("Windows resolver: numeric fast path, copied host, pending outputs, retained completion and nonblocking cancellation passed");

	return 0;
}
