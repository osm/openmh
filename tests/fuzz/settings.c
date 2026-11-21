#include <stdlib.h>
#include <string.h>

#include "../support/settings_catalog.h"

int	LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

int
LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	struct mh_settings settings, before;
	char encoded[MH_SETTINGS_TEXT_CAPACITY];
	size_t written;
	unsigned long random;

	if (mh_settings_init(&settings, "Player", 100, 100, 0, NULL) !=
		MH_ERROR_NONE)
		abort();

	memcpy(&before, &settings, sizeof(before));
	random = 1;

	if (mh_settings_parse(data, size, &settings_catalog, &random, &settings,
		NULL) == MH_ERROR_NONE) {
		if (mh_settings_encode(&settings, &settings_catalog, encoded,
			sizeof(encoded), &written, NULL) == MH_ERROR_NONE)
			mh_settings_parse((const unsigned char *)encoded,
				written, &settings_catalog, &random, &settings,
				NULL);
	} else if (memcmp(&settings, &before, sizeof(settings)) != 0 ||
		random != 1)
		abort();

	return 0;
}
