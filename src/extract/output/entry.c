#include "engine/assets/path_directories.h"
#include "engine/core/text.h"
#include "extract/output/entry.h"

enum mh_error_code
mh_extract_package_add(struct mh_zip_writer *writer,
	const struct mh_resource_source *source, const char *path,
	const char *name, int directory, struct mh_error *error_out)
{
	enum mh_zip_compression compression;

	compression = directory ||
			mh_ascii_equal_n(name, MH_ASSET_MUSIC_PREFIX,
				sizeof(MH_ASSET_MUSIC_PREFIX) - 1) ||
			mh_ascii_equal_n(name, MH_ASSET_MOVIES_PREFIX,
				sizeof(MH_ASSET_MOVIES_PREFIX) - 1)
		? MH_ZIP_STORE
		: MH_ZIP_COMPRESS;

	return mh_zip_writer_add(
		writer, source, path, name, directory, compression, error_out);
}
