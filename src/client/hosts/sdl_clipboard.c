#include <stddef.h>

#include "backends/input/sdl_provider.h"
#include "client/hosts/capabilities.h"
#include "client/hosts/sdl_clipboard.h"
#include "interfaces/input/clipboard_backend.h"

const struct mh_clipboard_provider *
mh_host_sdl_clipboard(void)
{
	return mh_capability_available(MH_CAP_CLIPBOARD)
		? mh_sdl_clipboard_provider()
		: NULL;
}
