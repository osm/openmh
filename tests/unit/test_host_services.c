#include <stdio.h>
#include <string.h>

#include "client/application/application.h"
#include "client/hosts/services.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "%d: %s\n", __LINE__, #x);             \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_app_services services;
	struct mh_error error;
	static const char directory[] = "/assets";

	/*
	 * A reused descriptor must not retain services selected by another
	 * host.
	 */
	memset(&services, 0xff, sizeof(services));
	CHECK(mh_host_common_services(&services, directory, NULL, &error) ==
		MH_ERROR_NONE);
	CHECK(services.game_directory == directory);
	CHECK(services.resources != NULL && services.resource_archives != NULL);
	CHECK(services.user_data != NULL && services.clock != NULL);
	CHECK(services.model_decoder != NULL && services.world_decoder != NULL);
	CHECK(services.image_source != NULL && services.audio_decoder != NULL);
	CHECK(services.catalog_source != NULL &&
		services.profile_source != NULL);
	CHECK(services.rendering == NULL && services.inputs == NULL);
	CHECK(services.audio == NULL && services.videos == NULL);
	CHECK(services.network == NULL && services.clipboard == NULL);
	CHECK(services.lifecycle == NULL && services.storage == NULL);
	CHECK(services.screenshot_data == NULL &&
		services.screenshot_directory == NULL);
	CHECK(services.audio_trace == 0 && services.network_trace == 0);

	return 0;
}
