#include <stdlib.h>

#include "backends/font/fnt_provider.h"
#include "engine/assets/font.h"
#include "engine/storage/asset_path.h"
#include "interfaces/font/source_backend.h"
#include "interfaces/storage/resource.h"

static enum mh_error_code
read_font(const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *output,
	struct mh_error *error_out)
{
	char path[1024];
	enum mh_error_code status;

	status = mh_asset_path_from(
		resources, root, reference, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_resource_source_read(resources, path, output, error_out);
}

static enum mh_error_code
load_glyphs(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_font *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status = read_font(resources, root, reference, &file, error_out);

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
	status = read_font(resources, root, reference, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_font_fnt_decode_program(
		file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

const struct mh_font_source_provider *
mh_font_fnt_source(void)
{
	static const struct mh_font_source_ops ops = {
		load_glyphs, load_program};

	static const struct mh_font_source_provider provider = {&ops, NULL};

	return &provider;
}
