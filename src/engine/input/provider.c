#include <stdlib.h>
#include <string.h>

#include "interfaces/input/backend.h"

struct mh_input_backend {
	const struct mh_input_provider	*provider;
	void				*context;
};

static int
complete_ops(const struct mh_input_ops *ops)
{
	return ops != NULL && ops->open != NULL && ops->close != NULL &&
		ops->focused != NULL && ops->event != NULL &&
		ops->poll != NULL && ops->key_down != NULL &&
		ops->characters != NULL && ops->mouse_grab != NULL &&
		(ops->force == NULL ||
			(ops->force->poll != NULL &&
				ops->force->apply != NULL &&
				ops->force->stop != NULL));
}

enum mh_error_code
mh_input_provider_init(struct mh_input_provider *provider,
	const struct mh_input_ops *ops, void *context,
	struct mh_error *error_out)
{
	if (provider == NULL || !complete_ops(ops))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete input provider");

	provider->ops = ops;
	provider->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_input_open_from(const struct mh_input_provider *provider,
	struct mh_input *input, struct mh_window *window, int grab_mouse,
	struct mh_error *error_out)
{
	struct mh_input_backend *backend;
	enum mh_error_code status;

	if (input == NULL || input->backend != NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid input lifetime");

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"input is unavailable");

	if (grab_mouse != 0 && grab_mouse != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid mouse grab preference");

	if (!complete_ops(provider->ops))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete input provider");

	backend = malloc(sizeof(*backend));

	if (backend == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening input");

	backend->provider = provider;
	backend->context = NULL;
	status = provider->ops->open(provider->context, window, grab_mouse,
		&backend->context, error_out);

	if (status != MH_ERROR_NONE) {
		free(backend);
		return status;
	}

	input->backend = backend;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_input_set_mouse_grab(
	struct mh_input *input, int enabled, struct mh_error *error_out)
{
	struct mh_input_backend *backend;

	if (input == NULL || input->backend == NULL)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "input is not open");

	if (enabled != 0 && enabled != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid mouse grab preference");

	backend = input->backend;

	return backend->provider->ops->mouse_grab(backend->provider->context,
		backend->context, enabled, error_out);
}

void
mh_input_close(struct mh_input *input)
{
	struct mh_input_backend *backend;

	if (input == NULL || input->backend == NULL)
		return;

	backend = input->backend;
	mh_input_force_stop(input);
	backend->provider->ops->close(
		backend->provider->context, backend->context);
	free(backend);
	input->backend = NULL;
}

int
mh_input_focused(const struct mh_input *input)
{
	const struct mh_input_backend *backend;

	if (input == NULL || input->backend == NULL)
		return 0;

	backend = input->backend;

	return backend->provider->ops->focused(
		backend->provider->context, backend->context);
}

int
mh_input_event(struct mh_input *input, struct mh_event *event)
{
	struct mh_input_backend *backend;

	if (input == NULL || input->backend == NULL || event == NULL)
		return 0;

	backend = input->backend;

	return backend->provider->ops->event(
		backend->provider->context, backend->context, event);
}

int
mh_input_provider_event(
	const struct mh_input_provider *provider, struct mh_event *event)
{
	if (provider == NULL || event == NULL)
		return 0;

	return provider->ops->event(provider->context, NULL, event);
}

enum mh_error_code
mh_input_poll(struct mh_input *input, struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now_ms,
	unsigned long mode, struct mh_device_input *snapshot,
	struct mh_error *error_out)
{
	struct mh_input_backend *backend;

	if (input == NULL || input->backend == NULL)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "input is not open");

	if (keyboard == NULL || down == NULL || snapshot == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing input poll arguments");

	backend = input->backend;

	return backend->provider->ops->poll(backend->provider->context,
		backend->context, keyboard, down, now_ms, mode, snapshot,
		error_out);
}

int
mh_input_key_down(const struct mh_input_provider *provider, unsigned int scan)
{
	if (provider == NULL || scan == 0 || scan >= MH_KEY_COUNT)
		return 0;

	return provider->ops->key_down(provider->context, scan);
}

void
mh_input_characters(const struct mh_input_provider *provider,
	const unsigned char down[MH_KEY_COUNT],
	unsigned char characters[MH_KEY_COUNT])
{
	if (characters == NULL)
		return;

	memset(characters, 0, MH_KEY_COUNT);

	if (provider != NULL && down != NULL)
		provider->ops->characters(provider->context, down, characters);
}

int
mh_input_has_force(const struct mh_input_provider *provider)
{
	return provider != NULL && provider->ops != NULL &&
		provider->ops->force != NULL;
}

enum mh_error_code
mh_input_force_poll(struct mh_input *input,
	const struct mh_force_effect initial[MH_FORCE_EFFECTS],
	struct mh_force_status *output, struct mh_error *error_out)
{
	struct mh_input_backend *backend;

	if (input == NULL || input->backend == NULL || initial == NULL ||
		output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid feedback poll");

	backend = input->backend;

	if (!mh_input_has_force(backend->provider)) {
		memset(output, 0, sizeof(*output));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return backend->provider->ops->force->poll(backend->provider->context,
		backend->context, initial, output, error_out);
}

enum mh_error_code
mh_input_force_apply(struct mh_input *input,
	const struct mh_force_effect effects[MH_FORCE_EFFECTS],
	unsigned int changed, unsigned int start, struct mh_error *error_out)
{
	struct mh_input_backend *backend;

	if (input == NULL || input->backend == NULL || effects == NULL ||
		(changed | start) >= (1U << MH_FORCE_EFFECTS) ||
		(start & ~changed))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid feedback update");

	backend = input->backend;

	if (!mh_input_has_force(backend->provider))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"force feedback is unavailable");

	return backend->provider->ops->force->apply(backend->provider->context,
		backend->context, effects, changed, start, error_out);
}

void
mh_input_force_stop(struct mh_input *input)
{
	struct mh_input_backend *backend;

	if (input == NULL || input->backend == NULL)
		return;

	backend = input->backend;

	if (mh_input_has_force(backend->provider))
		backend->provider->ops->force->stop(
			backend->provider->context, backend->context);
}
