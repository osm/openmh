#ifndef MH_HOST_COMMON_SERVICES_H
#define MH_HOST_COMMON_SERVICES_H

#include "engine/core/error.h"

struct mh_app_services;
struct mh_storage;

/* Initialize shared asset/storage services; platform services remain unset. */
enum mh_error_code	mh_host_common_services(
	struct mh_app_services *services, const char *directory,
	struct mh_storage *storage, struct mh_error *error_out);

#endif
