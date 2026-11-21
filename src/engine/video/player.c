#include <stdio.h>
#include <stdlib.h>

#include "interfaces/video/backend.h"

struct mh_video_player {
	const struct mh_video_provider	*provider;
	struct mh_video_options		options;
	void				*context;
	int				*outcome_output;
	enum mh_video_status		status;
	enum mh_video_outcome		outcome;
	unsigned long			delay;
	struct mh_error			error;
	char				diagnostic[512];
};

static int
complete_ops(const struct mh_video_ops *ops)
{
	return ops != NULL && ops->open != NULL && ops->step != NULL &&
		ops->close != NULL;
}

enum mh_error_code
mh_video_provider_init(struct mh_video_provider *provider,
	const struct mh_video_ops *ops, void *context,
	struct mh_error *error_out)
{
	if (provider == NULL || !complete_ops(ops))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete movie provider");

	provider->ops = ops;
	provider->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_video_resolve_from(const struct mh_video_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *output, size_t capacity, int *found,
	struct mh_error *error_out)
{
	if (provider == NULL ||
		(provider->ops != NULL && provider->ops->resolve == NULL))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"video collection lookup is unavailable");

	if (!complete_ops(provider->ops) || resources == NULL || root == NULL ||
		reference == NULL || output == NULL || capacity == 0 ||
		found == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid video collection lookup");

	return provider->ops->resolve(provider->context, resources, root,
		reference, output, capacity, found, error_out);
}

enum mh_error_code
mh_video_open_from(const struct mh_video_provider *provider,
	const struct mh_video_options *options, int *outcome,
	struct mh_video_player **output, struct mh_error *error_out)
{
	struct mh_video_player *player;
	enum mh_error_code status;

	if (outcome != NULL)
		*outcome = MH_VIDEO_NO_OUTCOME;

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"movie playback is unavailable");

	if (!complete_ops(provider->ops) || options == NULL ||
		options->resources == NULL || options->reference == NULL ||
		(options->collection != 0 && options->collection != 1) ||
		(options->mute != 0 && options->mute != 1) ||
		(options->fullscreen != 0 && options->fullscreen != 1) ||
		(options->window != NULL && options->capture != NULL) ||
		outcome == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid movie request");

	player = calloc(1, sizeof(*player));

	if (player == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate movie player");

	player->provider = provider;
	player->options = *options;
	player->outcome_output = outcome;
	player->status = MH_VIDEO_RUNNING;
	player->error = mh_success;
	status = provider->ops->open(provider->context, &player->options,
		&player->context, error_out);

	if (status != MH_ERROR_NONE) {
		free(player);
		return status;
	}

	*output = player;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_video_status
mh_video_step(struct mh_video_player *player)
{
	struct mh_error error;
	enum mh_video_status status;

	if (player == NULL)
		return MH_VIDEO_FAILED;

	player->delay = 0;

	if (player->status != MH_VIDEO_RUNNING)
		return player->status;

	error = mh_success;
	status = player->provider->ops->step(player->provider->context,
		player->context, &player->delay, &player->outcome, &error);

	if ((status != MH_VIDEO_RUNNING && status != MH_VIDEO_DONE) ||
		(status == MH_VIDEO_DONE &&
			(player->outcome < MH_VIDEO_ENDED ||
				player->outcome > MH_VIDEO_CLOSED))) {
		status = MH_VIDEO_FAILED;

		if (error.code == MH_ERROR_NONE)
			mh_error_set(&error, MH_ERROR_BACKEND,
				"movie playback failed");

		mh_error_copy(&player->error, &error, player->diagnostic,
			sizeof(player->diagnostic));
	}

	if (status != MH_VIDEO_RUNNING)
		player->delay = 0;

	player->status = status;

	return status;
}

unsigned long
mh_video_delay(const struct mh_video_player *player)
{
	return player != NULL ? player->delay : 0;
}

enum mh_error_code
mh_video_error(const struct mh_video_player *player, struct mh_error *error_out)
{
	if (player == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing movie player");

	return mh_error_forward(error_out, &player->error);
}

int
mh_video_close(struct mh_video_player *player)
{
	int success;

	if (player == NULL)
		return 0;

	success = player->status == MH_VIDEO_DONE;

	if (player->status == MH_VIDEO_FAILED)
		fprintf(stderr, "Movie playback: %s\n", player->error.message);

	player->provider->ops->close(
		player->provider->context, player->context);

	if (success)
		*player->outcome_output = player->outcome;

	free(player);

	return success;
}
