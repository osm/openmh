#include <stdlib.h>

#include "backends/font/fnt_provider.h"
#include "backends/font/provider.h"
#include "engine/assets/demo_font.h"
#include "engine/assets/demo_layout.h"
#include "engine/assets/font.h"
#include "engine/assets/path_fonts.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "interfaces/font/source_backend.h"
#include "interfaces/storage/resource.h"

static enum mh_error_code
read_small_font(const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *output,
	struct mh_error *error_out)
{
	const struct mh_demo_asset_layout *layout;
	struct mh_file file;
	char path[1024];
	enum mh_error_code status;

	if (!mh_ascii_equal(reference, MH_ASSET_FONT1S))
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"no alternate font for this reference");

	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((layout->fallbacks & MH_DEMO_FALLBACK_SMALL_FONT) == 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"installation supplies its own small font");

	status = mh_asset_path_from(resources, root, MH_ASSET_FONT2S, path,
		sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read_bounded(
			resources, path, 1024UL * 1024, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_font_fnt_byte_pixels(&file, output, error_out);
	free(file.data);

	return status;
}

static enum mh_error_code
load_glyphs(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_font *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status = mh_font_load(mh_font_fnt_source(), resources, root, reference,
		output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	status = read_small_font(resources, root, reference, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_font_fnt_decode(file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

static enum mh_error_code
load_program(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_font_program *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status = mh_font_load_program(mh_font_fnt_source(), resources, root,
		reference, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	status = read_small_font(resources, root, reference, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_font_fnt_decode_program(
		file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

const struct mh_font_source_provider *
mh_font_fallback_source(void)
{
	static const struct mh_font_source_ops ops = {
		load_glyphs, load_program};

	static const struct mh_font_source_provider provider = {&ops, NULL};

	return &provider;
}
