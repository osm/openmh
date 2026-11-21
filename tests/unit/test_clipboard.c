#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interfaces/input/clipboard_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "clipboard line %d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct pending_read {
	int		complete;
	const char	*text;
};

struct fixture {
	struct pending_read	*pending;
	unsigned int		opened, reads, closed, cancelled;
	int			deny_open, fail_read;
	char			diagnostic[80];
};

static enum mh_error_code
begin(void *context, void **output, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct pending_read *request;

	fixture = context;

	if (fixture->deny_open)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"clipboard permission denied");

	request = calloc(1, sizeof(*request));

	if (request == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate test request");

	fixture->pending = request;
	fixture->opened++;
	*output = request;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_clipboard_status
read_result(
	void *context, void *token, char **output, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct pending_read *request;
	char *text;

	fixture = context;
	request = token;
	fixture->reads++;

	if (fixture->fail_read) {
		mh_error_set(
			error_out, MH_ERROR_PERMISSION, fixture->diagnostic);
		return MH_CLIPBOARD_FAILED;
	}

	if (!request->complete) {
		mh_error_set(error_out, MH_ERROR_NONE, NULL);
		return MH_CLIPBOARD_PENDING;
	}

	text = malloc(strlen(request->text) + 1);

	if (text == NULL) {
		mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate test text");
		return MH_CLIPBOARD_FAILED;
	}

	strcpy(text, request->text);
	*output = text;
	mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return MH_CLIPBOARD_READY;
}

static void
close_request(void *context, void *token)
{
	struct fixture *fixture;
	struct pending_read *request;

	fixture = context;
	request = token;

	if (!request->complete && !fixture->fail_read)
		fixture->cancelled++;

	fixture->pending = NULL;
	fixture->closed++;
	free(request);
}

static void
complete(struct fixture *fixture, const char *text)
{
	/* A detached host callback cannot reach a released request. */
	if (fixture->pending == NULL)
		return;

	fixture->pending->text = text;
	fixture->pending->complete = 1;
}

int
main(void)
{
	static const struct mh_clipboard_ops ops = {
		begin, read_result, close_request};

	static const char bytes[] = "\345\344\366";
	struct fixture first_state, second_state;
	struct mh_clipboard_provider first, second;
	struct mh_clipboard_ops incomplete;
	struct mh_clipboard *first_request, *second_request, *unchanged;
	struct mh_error error;
	unsigned int reads;

	memset(&first_state, 0, sizeof(first_state));
	memset(&second_state, 0, sizeof(second_state));
	CHECK(mh_clipboard_provider_init(&first, &ops, &first_state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clipboard_provider_init(&second, &ops, &second_state, NULL) ==
		MH_ERROR_NONE);
	first_request = NULL;
	CHECK(mh_clipboard_open(NULL, &first_request, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(first_request == NULL);
	first_state.deny_open = 1;
	CHECK(mh_clipboard_open(&first, &first_request, &error) ==
		MH_ERROR_PERMISSION);
	CHECK(first_request == NULL && first_state.opened == 0 &&
		first_state.closed == 0);
	first_state.deny_open = 0;
	CHECK(mh_clipboard_open(&first, &first_request, &error) ==
		MH_ERROR_NONE);
	CHECK(error.message == NULL && first_state.opened == 1 &&
		first_state.reads == 0);
	CHECK(mh_clipboard_open(&second, &second_request, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clipboard_result(first_request) == NULL);
	CHECK(mh_clipboard_step(first_request) == MH_CLIPBOARD_PENDING);
	CHECK(mh_clipboard_step(second_request) == MH_CLIPBOARD_PENDING);
	CHECK(mh_clipboard_step(first_request) == MH_CLIPBOARD_PENDING);
	complete(&second_state, bytes);
	CHECK(mh_clipboard_step(second_request) == MH_CLIPBOARD_READY);
	CHECK(strcmp(mh_clipboard_result(second_request), bytes) == 0);
	CHECK(mh_clipboard_result(first_request) == NULL);
	reads = second_state.reads;
	CHECK(mh_clipboard_step(second_request) == MH_CLIPBOARD_READY &&
		second_state.reads == reads);
	complete(&second_state, "changed host clipboard");
	CHECK(strcmp(mh_clipboard_result(second_request), bytes) == 0);
	mh_clipboard_close(second_request);
	CHECK(second_state.closed == 1 && second_state.cancelled == 0);
	mh_clipboard_close(first_request);
	CHECK(first_state.closed == 1 && first_state.cancelled == 1 &&
		first_state.pending == NULL);
	complete(&first_state, "late completion after close");
	CHECK(first_state.pending == NULL);
	CHECK(mh_clipboard_open(&first, &first_request, NULL) == MH_ERROR_NONE);
	complete(&first_state, "");
	CHECK(mh_clipboard_step(first_request) == MH_CLIPBOARD_READY);
	CHECK(mh_clipboard_result(first_request) != NULL &&
		*mh_clipboard_result(first_request) == 0);
	mh_clipboard_close(first_request);
	CHECK(first_state.closed == 2 && first_state.cancelled == 1);
	CHECK(mh_clipboard_open(&first, &first_request, NULL) == MH_ERROR_NONE);
	CHECK(mh_clipboard_step(first_request) == MH_CLIPBOARD_PENDING);
	first_state.fail_read = 1;
	strcpy(first_state.diagnostic, "clipboard access revoked");
	CHECK(mh_clipboard_step(first_request) == MH_CLIPBOARD_FAILED);
	CHECK(mh_clipboard_result(first_request) == NULL);
	CHECK(mh_clipboard_error(first_request, &error) == MH_ERROR_PERMISSION);
	strcpy(first_state.diagnostic, "provider reused its diagnostic");
	CHECK(strcmp(error.message, "clipboard access revoked") == 0);
	reads = first_state.reads;
	CHECK(mh_clipboard_step(first_request) == MH_CLIPBOARD_FAILED &&
		first_state.reads == reads);
	unchanged = first_request;
	incomplete = ops;
	incomplete.close = NULL;
	CHECK(mh_clipboard_provider_init(&first, &incomplete, &second_state,
		NULL) == MH_ERROR_INVALID);
	CHECK(first.ops == &ops && first.context == &first_state);
	first_state.deny_open = 1;
	CHECK(mh_clipboard_open(&first, &unchanged, NULL) ==
		MH_ERROR_PERMISSION);
	CHECK(unchanged == first_request);
	mh_clipboard_close(first_request);
	CHECK(first_state.closed == first_state.opened &&
		first_state.pending == NULL);
	mh_clipboard_close(NULL);
	puts("Clipboard providers: pending, completion, cancellation, ownership and stable failures passed");

	return 0;
}
