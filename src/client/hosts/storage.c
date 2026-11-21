#include "backends/storage/filesystem_provider.h"
#include "client/hosts/storage.h"
#include "interfaces/storage/user_data_backend.h"

const struct mh_user_data *
mh_host_user_data(void)
{
#ifdef MH_HAVE_USER_DATA_FILES
	return mh_filesystem_user_data();
#else

	return NULL;
#endif
}
