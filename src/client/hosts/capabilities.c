#include "client/hosts/capabilities.h"

int
mh_capability_available(enum mh_capability capability)
{
	switch (capability) {
#ifdef MH_HOST_NETWORK
	case MH_CAP_NETWORK:
		return 1;
#endif
#ifdef MH_HOST_VIDEO
	case MH_CAP_VIDEO:
		return 1;
#endif
#ifdef MH_HOST_CLIPBOARD
	case MH_CAP_CLIPBOARD:
		return 1;
#endif
#ifdef MH_HOST_SCREENSHOTS
	case MH_CAP_SCREENSHOTS:
		return 1;
#endif
	default:
		return 0;
	}
}
