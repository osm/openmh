#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interfaces/network/resolver_backend.h"

#define LOOPBACK 0x7f000001UL
static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "resolver line %d: %s\n", __LINE__,    \
				#e);                                           \
			failures++;                                            \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	opened, closed;
	int		ready, fail;
	unsigned long	address;
};

static enum mh_error_code
begin(void *context, const char *host, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	char *name;

	fixture = context;

	if (fixture->fail)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "open refused");

	name = malloc(strlen(host) + 1);

	if (name == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "allocation failed");

	strcpy(name, host);
	fixture->opened++;
	*output = name;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
poll(void *context, void *request, int *done, unsigned long *address,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;
	CHECK(strcmp(request, "host.example") == 0);

	if (fixture->fail)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "host not found");

	if (fixture->ready)
		*address = fixture->address;

	*done = fixture->ready;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_request(void *context, void *request)
{
	struct fixture *fixture;

	fixture = context;
	fixture->closed++;
	free(request);
}

int
main(void)
{
	static const struct mh_resolver_ops ops = {begin, poll, close_request};
	struct mh_resolver_provider first_provider, second_provider;
	struct fixture first, second;
	struct mh_resolver *first_request, *second_request, *output;
	struct mh_error error;
	unsigned long address;
	char host[32];
	int done;

	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	first.address = LOOPBACK;
	second.address = LOOPBACK + 1;
	first_request = second_request = NULL;
	CHECK(mh_resolver_provider_init(&first_provider, &ops, &first, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_resolver_provider_init(
		&second_provider, &ops, &second, NULL) == MH_ERROR_NONE);
	strcpy(host, "host.example");
	CHECK(mh_resolver_open(&first_provider, host, &first_request, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_resolver_open(&second_provider, host, &second_request, NULL) ==
		MH_ERROR_NONE);
	strcpy(host, "changed");
	address = 99;
	done = 42;
	CHECK(mh_resolver_poll(first_request, &done, &address, NULL) ==
		MH_ERROR_NONE);
	CHECK(done == 0 && address == 99);
	first.ready = 1;
	CHECK(mh_resolver_poll(first_request, &done, &address, NULL) ==
		MH_ERROR_NONE);
	CHECK(done == 1 && address == LOOPBACK);
	CHECK(mh_resolver_poll(second_request, &done, &address, NULL) ==
		MH_ERROR_NONE);
	CHECK(done == 0 && address == LOOPBACK);
	second.fail = 1;
	CHECK(mh_resolver_poll(second_request, &done, &address, &error) ==
		MH_ERROR_NOT_FOUND);
	CHECK(done == 0 && address == LOOPBACK &&
		strcmp(error.message, "host not found") == 0);
	output = first_request;
	CHECK(mh_resolver_open(NULL, host, &output, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(output == first_request);
	CHECK(mh_resolver_open(&second_provider, host, &output, NULL) ==
		MH_ERROR_BACKEND);
	CHECK(output == first_request);
	mh_resolver_close(second_request);
	CHECK(second.closed == 1 && first.closed == 0);
	CHECK(mh_resolver_poll(first_request, &done, &address, NULL) ==
		MH_ERROR_NONE);
	CHECK(done == 1 && address == LOOPBACK);
	mh_resolver_close(first_request);
	mh_resolver_close(NULL);
	CHECK(first.opened == first.closed && second.opened == second.closed);

	return failures != 0;
}
