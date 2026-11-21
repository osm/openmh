#include <stdlib.h>

#include "interfaces/input/clipboard_backend.h"

enum {
	DIAGNOSTIC_SIZE = 256
};

struct mh_clipboard {
	const struct mh_clipboard_provider	*provider;
	void					*request;
	char					*text;
	enum mh_clipboard_status		status;
	struct mh_error				error;
	char					diagnostic[DIAGNOSTIC_SIZE];
};

static int
complete_ops(const struct mh_clipboard_ops *ops)
{
	return ops != NULL && ops->begin != NULL && ops->read != NULL &&
		ops->close != NULL;
}

enum mh_error_code
mh_clipboard_provider_init(struct mh_clipboard_provider *provider,
	const struct mh_clipboard_ops *ops, void *context,
	struct mh_error *error_out)
{
	if (provider == NULL || !complete_ops(ops))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete clipboard provider");

	provider->ops = ops;
	provider->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_clipboard_open(const struct mh_clipboard_provider *provider,
	struct mh_clipboard **output, struct mh_error *error_out)
{
	struct mh_clipboard *clipboard;
	enum mh_error_code status;

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"clipboard is unavailable");

	if (!complete_ops(provider->ops) || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid clipboard request");

	clipboard = calloc(1, sizeof(*clipboard));

	if (clipboard == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening clipboard request");

	clipboard->provider = provider;
	clipboard->status = MH_CLIPBOARD_PENDING;
	status = provider->ops->begin(
		provider->context, &clipboard->request, error_out);

	if (status != MH_ERROR_NONE) {
		free(clipboard);
		return status;
	}

	*output = clipboard;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_clipboard_status
mh_clipboard_step(struct mh_clipboard *clipboard)
{
	struct mh_error error;
	enum mh_clipboard_status status;

	if (clipboard == NULL)
		return MH_CLIPBOARD_FAILED;

	if (clipboard->status != MH_CLIPBOARD_PENDING)
		return clipboard->status;

	error = mh_success;
	status = clipboard->provider->ops->read(clipboard->provider->context,
		clipboard->request, &clipboard->text, &error);

	if (error.code != MH_ERROR_NONE ||
		(status != MH_CLIPBOARD_PENDING &&
			status != MH_CLIPBOARD_READY) ||
		(status == MH_CLIPBOARD_READY && clipboard->text == NULL)) {
		status = MH_CLIPBOARD_FAILED;

		if (error.code == MH_ERROR_NONE)
			mh_error_set(&error, MH_ERROR_BACKEND,
				"clipboard read failed");

		/*
		 * Keep borrowed diagnostics stable across later provider calls.
		 */
		mh_error_copy(&clipboard->error, &error, clipboard->diagnostic,
			sizeof(clipboard->diagnostic));
	}

	clipboard->status = status;

	return status;
}

const char *
mh_clipboard_result(const struct mh_clipboard *clipboard)
{
	return clipboard != NULL && clipboard->status == MH_CLIPBOARD_READY
		? clipboard->text
		: NULL;
}

enum mh_error_code
mh_clipboard_error(
	const struct mh_clipboard *clipboard, struct mh_error *error_out)
{
	if (clipboard == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing clipboard request");

	return mh_error_forward(error_out, &clipboard->error);
}

void
mh_clipboard_close(struct mh_clipboard *clipboard)
{
	if (clipboard == NULL)
		return;

	clipboard->provider->ops->close(
		clipboard->provider->context, clipboard->request);
	free(clipboard->text);
	free(clipboard);
}
