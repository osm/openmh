#include <string.h>

#include "backends/audio/ffmpeg_provider.h"
#include "backends/audio/provider.h"
#include "backends/audio/wave_provider.h"
#include "engine/assets/demo_layout.h"
#include "engine/assets/path_audio.h"
#include "engine/audio/sample_path.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "interfaces/audio/decoder_backend.h"

/* The first software demo uses majcrash.wav for both missing crash samples. */
static enum mh_error_code
resolve_demo_crash_sample(const struct mh_resource_source *resources,
	const char *root, const char *reference, char *path, size_t capacity,
	struct mh_error *error_out)
{
	const struct mh_demo_asset_layout *layout;
	enum mh_error_code status;

	if (!mh_ascii_equal(reference, MH_ASSET_SOUND_BARREL) &&
		!mh_ascii_equal(reference, MH_ASSET_SOUND_CRASH2))
		return MH_ERROR_NOT_FOUND;

	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (layout->crash_sample == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"installation has no alternate crash sample");

	return mh_asset_path_from(resources, root, layout->crash_sample, path,
		capacity, error_out);
}

static enum mh_error_code
resolve(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, char *path, size_t capacity,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;
	status = mh_audio_sample_file_path(mh_audio_wave_decoder(), resources,
		root, reference, path, capacity, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

#ifdef MH_DECODE_OPUS
	status = mh_audio_sample_file_path(mh_audio_ffmpeg_decoder(), resources,
		root, reference, path, capacity, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

#endif

	return resolve_demo_crash_sample(
		resources, root, reference, path, capacity, error_out);
}

static const struct mh_audio_decoder_provider *
select_decoder(const char *path)
{
	const char *suffix;

	suffix = strrchr(path, '.');

	if (suffix != NULL && mh_ascii_equal(suffix, ".opus")) {
#ifdef MH_DECODE_OPUS
		return mh_audio_ffmpeg_decoder();
#else
		return NULL;
#endif
	}

	return mh_audio_wave_decoder();
}

static enum mh_error_code
inspect(void *context, const struct mh_resource_source *resources,
	const char *path, struct mh_audio_decode_info *output,
	struct mh_error *error_out)
{
	const struct mh_audio_decoder_provider *provider;

	(void)context;
	provider = select_decoder(path);

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Opus decoding is not included in this build");

	return mh_audio_decoder_inspect(
		provider, resources, path, output, error_out);
}

static enum mh_error_code
open_sample(void *context, const struct mh_resource_source *resources,
	const char *path, void **output, struct mh_audio_decode_info *info,
	struct mh_error *error_out)
{
	const struct mh_audio_decoder_provider *provider;
	struct mh_audio_decoder *decoder;
	enum mh_error_code status;

	(void)context;
	provider = select_decoder(path);

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Opus decoding is not included in this build");

	status = mh_audio_decoder_open(
		provider, resources, path, &decoder, error_out);

	if (status == MH_ERROR_NONE) {
		*info = *mh_audio_decoder_info(decoder);
		*output = decoder;
	}

	return status;
}

static enum mh_error_code
read_sample(void *context, void *decoder, void *output, size_t requested,
	size_t *count, struct mh_error *error_out)
{
	const struct mh_audio_decode_info *info;
	size_t capacity;

	(void)context;
	info = mh_audio_decoder_info(decoder);
	capacity = requested * info->channels * (info->bits / 8);

	return mh_audio_decoder_read(
		decoder, output, capacity, requested, count, error_out);
}

static enum mh_error_code
seek_sample(
	void *context, void *decoder, size_t frame, struct mh_error *error_out)
{
	(void)context;

	return mh_audio_decoder_seek(decoder, frame, error_out);
}

static void
close_sample(void *context, void *decoder)
{
	(void)context;
	mh_audio_decoder_close(decoder);
}

const struct mh_audio_decoder_provider *
mh_audio_fallback_decoder(void)
{
	static const struct mh_audio_decoder_ops ops = {
		inspect, open_sample, read_sample, seek_sample, close_sample};

	static const struct mh_audio_decoder_provider provider = {
		"wav", &ops, NULL, resolve};

	return &provider;
}
