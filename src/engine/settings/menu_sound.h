#ifndef MH_SETTINGS_MENU_SOUND_H
#define MH_SETTINGS_MENU_SOUND_H

#include <stddef.h>

#include "engine/core/error.h"

#define MH_MENU_SOUND_SAMPLES 37
#define MH_MENU_SOUND_EVENTS 38
#define MH_MENU_SOUND_NAME_BYTES 64

struct mh_menu_sound_sample {
	char	name[MH_MENU_SOUND_NAME_BYTES];
	float	volume, pitch;
};

struct mh_menu_sound_config {
	unsigned int			count;
	struct mh_menu_sound_sample	samples[MH_MENU_SOUND_SAMPLES];

	/* Zero means no sample; 1..count are one-based sample indices. */
	unsigned int			events[MH_MENU_SOUND_EVENTS];
};

/*
 * Ordered sample definitions, compound gain/pitch changes and
 * last-assignment-wins event bindings. Names are bounded basenames. No file I/O
 * or audio-device dependency. Failure is atomic.
 */
enum mh_error_code	mh_menu_sound_read(const unsigned char *text,
	size_t size, struct mh_menu_sound_config *output,
	struct mh_error *error_out);

/* Event zero, unmapped events and out-of-range events have no sample. */
unsigned int	mh_menu_sound_event(const struct mh_menu_sound_config *config,
	unsigned int event);

#endif
