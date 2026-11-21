#include <stdlib.h>
#include <string.h>

#include "backends/material/internal.h"
#include "engine/assets/material.h"
#include "engine/assets/path_tracks.h"
#include "engine/assets/tbl.h"
#include "engine/core/file.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

enum mh_error_code
mh_materials_mat_srf_load(const struct mh_resource_source *source,
	const char *root, const char *srf, struct mh_materials *materials,
	struct mh_error *error_out)
{
	struct mh_file mat, surfaces;
	unsigned char *text;
	size_t text_size;
	char path[1024];
	enum mh_error_code status;

	memset(&mat, 0, sizeof(mat));
	memset(&surfaces, 0, sizeof(surfaces));
	text = NULL;
	status = mh_asset_path_from(source, root, MH_ASSET_MATERIALS, path,
		sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read(source, path, &mat, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_tbl_decode(
			mat.data, mat.size, &text, &text_size, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_asset_path_from(
			source, root, srf, path, sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read(
			source, path, &surfaces, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_materials_mat_srf_decode(text, text_size,
			surfaces.data, surfaces.size, materials, error_out);

	free(mat.data);
	free(surfaces.data);
	free(text);

	return status;
}
