#include <string.h>

#include "engine/catalog/track.h"

static int
matches_transparent_name(
	const struct mh_track *track, const char *name, int prefix)
{
	size_t at, i;
	const char *entry;
	unsigned char a, b;

	if (track == NULL || name == NULL || *name == 0 ||
		track->zero_transparent_size >= sizeof(track->zero_transparent))
		return 0;

	at = 0;

	while (at < track->zero_transparent_size) {
		entry = track->zero_transparent + at;

		if (*entry == 0 ||
			memchr(entry, 0, track->zero_transparent_size - at) ==
				NULL)
			return 0;

		/*
		 * Material flags use a prefix test. Texture loading uses full
		 * equality with ASCII case folding.
		 */
		for (i = 0; entry[i] != 0 && name[i] != 0; i++) {
			a = (unsigned char)entry[i];
			b = (unsigned char)name[i];

			if (a >= 'A' && a <= 'Z')
				a += 'a' - 'A';

			if (b >= 'A' && b <= 'Z')
				b += 'a' - 'A';

			if (a != b)
				break;
		}

		if (prefix ? (entry[i] == 0 || name[i] == 0)
			: (entry[i] == 0 && name[i] == 0))
			return 1;

		at += strlen(entry) + 1;
	}

	return 0;
}

int
mh_track_zero_transparent(const struct mh_track *track, const char *name)
{
	return matches_transparent_name(track, name, 1);
}

int
mh_track_texture_cutout(const struct mh_track *track, const char *name)
{
	return matches_transparent_name(track, name, 0);
}
