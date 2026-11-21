#include <string.h>

#include "engine/core/text.h"
#include "server/logging/events.h"

void
mh_server_event_log(const struct mh_server_event_logger *logger,
	const char *prefix, const char *suffix)
{
	char text[MH_SERVER_EVENT_BYTES];
	size_t used, length, i;

	if (logger == NULL || logger->write == NULL)
		return;

	used = strlen(prefix);

	if (used >= sizeof(text))
		used = sizeof(text) - 1;

	memcpy(text, prefix, used);

	if (suffix != NULL) {
		length = strlen(suffix);

		if (length >= sizeof(text) - used)
			length = sizeof(text) - used - 1;

		memcpy(text + used, suffix, length);
		used += length;
	}

	for (i = 0; i < used; i++)
		if (!mh_text_display_byte((unsigned char)text[i]))
			text[i] = '?';

	text[used] = 0;
	logger->write(logger->context, text);
}
