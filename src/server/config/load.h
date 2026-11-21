#ifndef MH_SERVER_CONFIG_LOAD_H
#define MH_SERVER_CONFIG_LOAD_H

#include "server/config/config.h"

struct mh_resource_source;

enum mh_error_code	mh_server_config_load(
	const struct mh_resource_source *source, const char *path,
	struct mh_server_config *config, size_t *error_line,
	struct mh_error *error_out);
enum mh_error_code	mh_server_config_validate(
	const struct mh_server_config *config, struct mh_error *error_out);

#endif
