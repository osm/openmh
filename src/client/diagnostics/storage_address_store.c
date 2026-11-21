#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_address_store.h"
#include "engine/storage/address_store.h"
#include "interfaces/storage/data_directory_backend.h"

enum mh_error_code
mh_address_store_open(const char *directory, const char *asset_root,
	struct mh_address_store **output, struct mh_error *error_out)
{
	struct mh_data_directory *data;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing address book output");

	status = mh_filesystem_data_directory_open(
		directory, asset_root, &data, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_address_store_open_in(data, output, error_out);

	if (status != MH_ERROR_NONE)
		mh_data_directory_close(data);

	return status;
}
