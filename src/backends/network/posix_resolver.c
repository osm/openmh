#define _POSIX_C_SOURCE 200112L

#include <sys/socket.h>

#include <arpa/inet.h>

#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/posix_provider.h"
#include "interfaces/network/resolver_backend.h"

struct lookup {
	pthread_mutex_t		mutex;

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

	pthread_mutex_lock(&lookup->mutex);
	destroy = --lookup->references == 0;
	pthread_mutex_unlock(&lookup->mutex);

	if (destroy) {
		pthread_mutex_destroy(&lookup->mutex);
		free(lookup->host);
		free(lookup);
	}
}

static void *
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

	pthread_mutex_lock(&lookup->mutex);
	lookup->ipv4 = ipv4;
	lookup->status = status;
	lookup->done = 1;
	pthread_mutex_unlock(&lookup->mutex);
	release_lookup(lookup);

	return NULL;
}

static enum mh_error_code
open_lookup(void *context, const char *host, void **output,
	struct mh_error *error_out)
{
	struct lookup *lookup;
	struct in_addr address;
	pthread_attr_t attributes;
	pthread_t worker;
	size_t length;
	int result;

	(void)context;
	lookup = calloc(1, sizeof(*lookup));

	if (lookup == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate native hostname lookup");

	if (pthread_mutex_init(&lookup->mutex, NULL) != 0) {
		free(lookup);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot initialize hostname lookup lock");
	}

	lookup->references = 1;

	/* inet_pton cannot perform name-service I/O. */
	if (inet_pton(AF_INET, host, &address) == 1) {
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
	result = pthread_attr_init(&attributes);

	if (result == 0) {
		result = pthread_attr_setdetachstate(
			&attributes, PTHREAD_CREATE_DETACHED);

		if (result == 0) {
			lookup->references = 2;
			result = pthread_create(
				&worker, &attributes, resolve_host, lookup);

			if (result != 0)
				lookup->references = 1;
		}

		pthread_attr_destroy(&attributes);
	}

	if (result != 0) {
		release_lookup(lookup);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot start hostname lookup worker");
	}

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
	pthread_mutex_lock(&lookup->mutex);
	finished = lookup->done;
	status = lookup->status;
	address = lookup->ipv4;
	pthread_mutex_unlock(&lookup->mutex);

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

	/* Cancel delivery; the detached worker releases its own reference. */
	release_lookup(request);
}

const struct mh_resolver_provider *
mh_posix_resolver_provider(void)
{
	static const struct mh_resolver_ops ops = {
		open_lookup, poll_lookup, close_lookup};

	static const struct mh_resolver_provider provider = {&ops, NULL};

	return &provider;
}
