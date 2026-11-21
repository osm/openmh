#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "backends/network/windows_provider.h"

struct lookup {
	CRITICAL_SECTION	lock;

	/* Caller and detached worker each retain one reference. */
	unsigned int		references;
	char			*host;
	int			done;
	enum mh_error_code	status;
	unsigned long		ipv4;
};

static void
release_lookup(struct lookup *lookup)
{
	int destroy;

	EnterCriticalSection(&lookup->lock);
	destroy = --lookup->references == 0;
	LeaveCriticalSection(&lookup->lock);

	if (destroy) {
		DeleteCriticalSection(&lookup->lock);
		WSACleanup();
		free(lookup->host);
		free(lookup);
	}
}

static unsigned int __stdcall
resolve_host(void *context)
{
	struct lookup *lookup;
	struct addrinfo hints, *addresses, *entry;
	const struct sockaddr_in *address;
	unsigned long ipv4;
	enum mh_error_code status;
	int result;

	lookup = context;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	addresses = NULL;
	ipv4 = 0;
	status = MH_ERROR_NOT_FOUND;
	result = getaddrinfo(lookup->host, NULL, &hints, &addresses);

	if (result == 0) {
		for (entry = addresses; entry != NULL; entry = entry->ai_next) {
			if (entry->ai_family != AF_INET ||
				entry->ai_addr == NULL ||
				entry->ai_addrlen < sizeof(*address))
				continue;

			address = (const struct sockaddr_in *)entry->ai_addr;
			ipv4 = ntohl(address->sin_addr.s_addr);
			status = MH_ERROR_NONE;
			break;
		}

		freeaddrinfo(addresses);
	} else if (result == EAI_MEMORY)
		status = MH_ERROR_MEMORY;
	else if (result != EAI_NONAME)
		status = MH_ERROR_IO;

	EnterCriticalSection(&lookup->lock);
	lookup->ipv4 = ipv4;
	lookup->status = status;
	lookup->done = 1;
	LeaveCriticalSection(&lookup->lock);
	release_lookup(lookup);

	return 0;
}

static enum mh_error_code
open_lookup(void *context, const char *host, void **output,
	struct mh_error *error_out)
{
	struct lookup *lookup;
	struct in_addr address;
	WSADATA data;
	uintptr_t worker;
	size_t length;

	(void)context;
	lookup = calloc(1, sizeof(*lookup));

	if (lookup == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate Windows hostname lookup");

	if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
		free(lookup);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot initialize hostname lookup Winsock");
	}

	if (!InitializeCriticalSectionEx(&lookup->lock, 0, 0)) {
		WSACleanup();
		free(lookup);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot initialize hostname lookup lock");
	}

	lookup->references = 1;

	/*
	 * Numeric addresses never start a worker or perform name-service I/O.
	 */
	if (InetPtonA(AF_INET, host, &address) == 1) {
		lookup->ipv4 = ntohl(address.s_addr);
		lookup->done = 1;
		*output = lookup;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	length = strlen(host);
	lookup->host = malloc(length + 1);

	if (lookup->host == NULL) {
		release_lookup(lookup);
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "cannot copy hostname");
	}

	memcpy(lookup->host, host, length + 1);
	lookup->references = 2;
	worker = _beginthreadex(NULL, 0, resolve_host, lookup, 0, NULL);

	if (worker == 0) {
		lookup->references = 1;
		release_lookup(lookup);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot start hostname lookup worker");
	}

	CloseHandle((HANDLE)worker);
	*output = lookup;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
poll_lookup(void *context, void *request, int *done, unsigned long *ipv4,
	struct mh_error *error_out)
{
	struct lookup *lookup;
	enum mh_error_code status;
	unsigned long address;
	int finished;

	(void)context;
	lookup = request;
	EnterCriticalSection(&lookup->lock);
	finished = lookup->done;
	status = lookup->status;
	address = lookup->ipv4;
	LeaveCriticalSection(&lookup->lock);

	if (finished && status != MH_ERROR_NONE)
		return mh_error_set(
			error_out, status, "cannot resolve IPv4 host");

	if (finished)
		*ipv4 = address;

	*done = finished;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_lookup(void *context, void *request)
{
	(void)context;

	/*
	 * Cancel delivery; the detached worker owns its data and Winsock
	 * lifetime.
	 */
	release_lookup(request);
}

const struct mh_resolver_provider *
mh_windows_resolver_provider(void)
{
	static const struct mh_resolver_ops ops = {
		open_lookup, poll_lookup, close_lookup};

	static const struct mh_resolver_provider provider = {&ops, NULL};

	return &provider;
}
