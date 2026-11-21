#include <string.h>

#include "backends/image/effects.h"
#include "backends/image/ilbm_provider.h"
#include "backends/image/provider.h"
#include "backends/image/tga_provider.h"
#include "engine/assets/demo_layout.h"
#include "engine/assets/demo_presentation.h"
#include "engine/assets/path_menus.h"
#include "engine/core/text.h"
#include "engine/image/load.h"
#include "interfaces/image/decoder_backend.h"

static const struct mh_image_decoder_provider *
select_decoder(const unsigned char *data, size_t size)
{
	if (size >= 12 && memcmp(data, "FORM", 4) == 0 &&
		memcmp(data + 8, "ILBM", 4) == 0)
		return mh_image_ilbm_decoder();

	return mh_image_tga_decoder();
}

static enum mh_error_code
read_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette_output, struct mh_error *error_out)
{
	(void)context;

	return mh_image_decode(select_decoder(data, size), data, size, options,
		output, palette_output, error_out);
}

static enum mh_error_code
read_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	(void)context;

	return mh_image_decode_indexed(
		select_decoder(data, size), data, size, output, error_out);
}

static enum mh_error_code
read_resource(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_file *output,
	struct mh_error *error_out)
{
	static const char *const names[] = {MH_ASSET_RESULTS_BACKDROP,
		MH_ASSET_RESULTS_TWO_BACKDROP, MH_ASSET_LOADING_BACKDROP,
		MH_ASSET_LOADING_ANIMATION};

	const struct mh_demo_asset_layout *layout;
	unsigned int image;
	enum mh_error_code status;

	(void)context;
	status = mh_image_file_read(mh_image_fallback_decoder(), resources,
		root, reference, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	for (image = 0; image < sizeof(names) / sizeof(names[0]); image++)
		if (mh_ascii_equal(reference, names[image]))
			break;

	if (image == sizeof(names) / sizeof(names[0]))
		return mh_image_effect_fallback(
			resources, root, reference, output, error_out);

	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((layout->fallbacks & MH_DEMO_FALLBACK_SUPPORT_ARTWORK) == 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"installation supplies its own presentation art");

	if (image < 2)
		return mh_demo_results_tga(
			resources, root, image, output, error_out);

	return mh_demo_loading_tga(image - 2, output, error_out);
}

const struct mh_image_decoder_provider *
mh_image_fallback_decoder(void)
{
	static const struct mh_image_decoder_ops ops = {
		read_rgba, read_indexed};

	static const struct mh_image_decoder_provider provider = {
		&ops, NULL, NULL, read_resource};

	return &provider;
}
