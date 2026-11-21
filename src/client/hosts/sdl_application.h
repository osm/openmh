#ifndef MH_HOST_SDL_APPLICATION_H
#define MH_HOST_SDL_APPLICATION_H

#include "client/application/application.h"

/* Select native platform services on top of the shared asset providers. */
enum mh_error_code	mh_host_sdl_services(struct mh_app_services *services,
	const char *directory, struct mh_storage *storage,
	const struct mh_render_service *rendering, struct mh_error *error_out);

/* Select native services and create the application's storage lifecycle. */
enum mh_error_code	mh_application_open(int argc, char **argv,
	struct mh_application **output, struct mh_error *error_out);

/*
 * Transfer a host-supplied storage service on success; failure leaves ownership
 * with the caller. PREPARE precedes all resource/settings access. COMMIT
 * follows application steps, including final saves, before the next step or
 * completion.
 */
enum mh_error_code	mh_application_open_with_storage(int argc, char **argv,
	struct mh_storage *storage, struct mh_application **output,
	struct mh_error *error_out);

#endif
