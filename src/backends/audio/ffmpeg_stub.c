#include "backends/audio/ffmpeg_provider.h"
#include "interfaces/audio/decoder_backend.h"

static enum mh_error_code
unavailable(struct mh_error *error_out)
{
	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"Opus playback requires the FFmpeg media libraries");
}

static enum mh_error_code
inspect(void *context, const struct mh_resource_source *resources,
	const char *path, struct mh_audio_decode_info *output,
	struct mh_error *error_out)
{
	(void)context;

	(void)resources;
	(void)path;
	(void)output;

	return unavailable(error_out);
}

static enum mh_error_code
open_decoder(void *context, const struct mh_resource_source *resources,
	const char *path, void **output, struct mh_audio_decode_info *info,
	struct mh_error *error_out)
{
	(void)output;

	return inspect(context, resources, path, info, error_out);
}

static enum mh_error_code
read_decoder(void *context, void *decoder, void *output, size_t requested,
	size_t *count, struct mh_error *error_out)
{
	(void)context;

	(void)decoder;
	(void)output;
	(void)requested;
	(void)count;

	return unavailable(error_out);
}

static enum mh_error_code
seek_decoder(
	void *context, void *decoder, size_t frame, struct mh_error *error_out)
{
	(void)context;

	(void)decoder;
	(void)frame;

	return unavailable(error_out);
}

static void
close_decoder(void *context, void *decoder)
{
	(void)context;
	(void)decoder;
}

const struct mh_audio_decoder_provider *
mh_audio_ffmpeg_decoder(void)
{
	static const struct mh_audio_decoder_ops ops = {inspect, open_decoder,
		read_decoder, seek_decoder, close_decoder};

	static const struct mh_audio_decoder_provider provider = {
		"opus", &ops, NULL, NULL};

	return &provider;
}
