#include <SDL3/SDL.h>
#include <stdlib.h>

#include "backends/input/sdl_provider.h"
#include "engine/core/charset.h"
#include "interfaces/input/clipboard_backend.h"

struct clipboard_request {
	char	*text;
};

static enum mh_error_code
read_text(char **output, struct mh_error *error_out)
{
	char *text;
	enum mh_error_code status;

	text = SDL_GetClipboardText();

	if (text == NULL)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot read clipboard text");

	status = mh_utf8_to_windows_1252(text, output, error_out);
	SDL_free(text);

	return status;
}

static enum mh_error_code
begin(void *context, void **output, struct mh_error *error_out)
{
	struct clipboard_request *request;
	enum mh_error_code status;

	(void)context;
	request = malloc(sizeof(*request));

	if (request == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening clipboard request");

	status = read_text(&request->text, error_out);

	if (status != MH_ERROR_NONE) {
		free(request);
		return status;
	}

	*output = request;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_clipboard_status
read_result(void *context, void *token, char **text, struct mh_error *error_out)
{
	struct clipboard_request *request;

	(void)context;
	request = token;

	/* Transfer the converted malloc-owned text to the portable request. */
	*text = request->text;
	request->text = NULL;
	mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return MH_CLIPBOARD_READY;
}

static void
close_request(void *context, void *token)
{
	struct clipboard_request *request;

	(void)context;
	request = token;
	free(request->text);
	free(request);
}

const struct mh_clipboard_provider *
mh_sdl_clipboard_provider(void)
{
	static const struct mh_clipboard_ops ops = {
		begin, read_result, close_request};

	static const struct mh_clipboard_provider provider = {&ops, NULL};

	return &provider;
}
