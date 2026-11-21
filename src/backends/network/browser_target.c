#include <stdlib.h>

#include "backends/network/browser_internal.h"
#include "engine/core/text.h"
#include "interfaces/network/backend.h"

static void
release_target(void *context, void *token)
{
	(void)context;
	free(token);
}

enum mh_error_code
mh_browser_network_target(const struct mh_browser_network_config *config,
	struct mh_transport_target **output, struct mh_error *error_out)
{
	struct mh_browser_target *target;
	const struct mh_browser_ice_server *ice;
	unsigned int i;
	enum mh_error_code status;

	if (config == NULL || output == NULL || config->signaling_url == NULL ||
		config->signaling_url[0] == 0 ||
		config->ice_server_count > MH_BROWSER_ICE_SERVERS ||
		(config->ice_server_count != 0 &&
			config->ice_servers == NULL) ||
		(config->relay_only != 0 && config->relay_only != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid browser network target");

	target = calloc(1, sizeof(*target));

	if (target == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate browser network target");

	target->ice_count = config->ice_server_count;
	target->relay_only = config->relay_only;
	status = mh_text_copy(target->url, sizeof(target->url),
		config->signaling_url, error_out);

	for (i = 0; i < target->ice_count && status == MH_ERROR_NONE; i++) {
		ice = &config->ice_servers[i];

		if (ice->url == NULL || ice->url[0] == 0) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"missing ICE server URL");
			break;
		}

		status = mh_text_copy(target->ice[i].url,
			sizeof(target->ice[i].url), ice->url, error_out);

		if (status == MH_ERROR_NONE && ice->username != NULL)
			status = mh_text_copy(target->ice[i].username,
				sizeof(target->ice[i].username), ice->username,
				error_out);

		if (status == MH_ERROR_NONE && ice->credential != NULL)
			status = mh_text_copy(target->ice[i].credential,
				sizeof(target->ice[i].credential),
				ice->credential, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_transport_target_create(
			mh_browser_network_provider(), target, release_target,
			output, error_out);

	if (status != MH_ERROR_NONE)
		free(target);

	return status;
}
