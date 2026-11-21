#include <string.h>

#include "engine/core/arguments.h"
#include "engine/core/limits.h"
#include "engine/network/endpoint.h"

enum mh_error_code
mh_network_endpoint_parse(const char *text, unsigned int default_port,
	struct mh_network_endpoint *output, struct mh_error *error_out)
{
	struct mh_network_endpoint next;
	const char *separator;
	size_t length, i;
	unsigned long port;

	if (text == NULL || text[0] == 0 || output == NULL ||
		default_port > MH_U16_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid server address");

	separator = strchr(text, ':');
	length = separator == NULL ? strlen(text) : (size_t)(separator - text);
	port = default_port;

	if (length == 0 ||
		(separator != NULL &&
			(!mh_arguments_unsigned(
				separator + 1, MH_U16_MAX, &port) ||
				port == 0)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected HOST or HOST:PORT, with port from 1 to 65535");

	if (length >= sizeof(next.host))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"server hostname is too long");

	for (i = 0; i < length; i++) {
		if (!((text[i] >= 'a' && text[i] <= 'z') ||
			(text[i] >= 'A' && text[i] <= 'Z') ||
			(text[i] >= '0' && text[i] <= '9') ||
			text[i] == '.' || text[i] == '-' || text[i] == '_'))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"expected an IPv4 address or hostname");
	}

	memset(&next, 0, sizeof(next));
	memcpy(next.host, text, length);
	next.port = (unsigned int)port;
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
