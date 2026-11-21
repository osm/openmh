#ifndef MH_AUDIO_HORN_H
#define MH_AUDIO_HORN_H

#include "engine/core/error.h"
#include "interfaces/audio/horn.h"

/*
 * Case-insensitive display-name lookup; never interpret a name as a path.
 * Unlike the original random fallback, absent/ambiguous names are errors.
 */
enum mh_error_code	mh_horn_find(const struct mh_horn_catalog *catalog,
	const char *name, unsigned int *index, struct mh_error *error_out);

/* Prefer Honker1 when supplied; otherwise select the first valid choice. */
enum mh_error_code	mh_horn_default(const struct mh_horn_catalog *catalog,
	unsigned int *index, struct mh_error *error_out);

/*
 * Profile lookup: first matching name, otherwise srand from the first four name
 * bytes (little endian), then rand()%count. Call during profile initialization,
 * not every race setup. Name is a bounded 16-byte profile field; unknown names
 * shorter than four bytes are rejected because the seed needs four bytes.
 * Atomic errors.
 */
enum mh_error_code	mh_horn_profile_find(
	const struct mh_horn_catalog *catalog, const char name[16],
	unsigned long *random_state, unsigned int *index,
	struct mh_error *error_out);

/*
 * Race binding applies the factors twice. Returned gain is unclipped;
 * positional attenuation precedes clamping.
 */
enum mh_error_code	mh_horn_parameters(const struct mh_horn *horn,
	double *volume, double *pitch, struct mh_error *error_out);

/*
 * Menu loading applies each factor once: gain=f32(volume*1.5),
 * pitch=f32(pitch). Playback adds nonpositional flags 4 and priority 4.
 */
enum mh_error_code	mh_horn_preview_parameters(const struct mh_horn *horn,
	double *volume, double *pitch, struct mh_error *error_out);

#endif
