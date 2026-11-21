#ifndef MH_A3D20_ACOUSTICS_H
#define MH_A3D20_ACOUSTICS_H

#include "engine/audio/a3d20_paths.h"

struct mh_mixer_processor;

struct mh_a3d20_acoustics;

/*
 * Native occlusion/filtering and bounded first-order reflection delay bus.
 * Owns fixed delay storage allocated at creation. No HRTF or late reverb.
 * Serialized with queue/render operations; no allocation in update/callbacks.
 */
enum mh_error_code	mh_a3d20_acoustics_create(unsigned long rate,
	struct mh_a3d20_acoustics **output, struct mh_error *error_out);
void			mh_a3d20_acoustics_destroy(
	struct mh_a3d20_acoustics *acoustics);
void			mh_a3d20_acoustics_clear(
	struct mh_a3d20_acoustics *acoustics);

/*
 * Restart a zero-based physical slot below MH_MIXER_VOICES; already emitted
 * echoes keep travelling.
 */
void	mh_a3d20_acoustics_restart(struct mh_a3d20_acoustics *acoustics,
	unsigned int slot);

/*
 * Same slot bound; validated solver output ramps gain/HF over five
 * milliseconds.
 */
void	mh_a3d20_acoustics_update(struct mh_a3d20_acoustics *acoustics,
	unsigned int slot, const struct mh_a3d20_paths *paths);

/*
 * Borrowed processor must not outlive acoustics. Mono positional sources only;
 * pause freezes the bus.
 */
void	mh_a3d20_acoustics_processor(struct mh_a3d20_acoustics *acoustics,
	struct mh_mixer_processor *processor);

#endif
