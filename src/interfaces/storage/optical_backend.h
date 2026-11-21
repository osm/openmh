#ifndef MH_STORAGE_OPTICAL_BACKEND_H
#define MH_STORAGE_OPTICAL_BACKEND_H

#include "interfaces/storage/optical.h"

struct mh_optical_ops {
	/*
	 * Exact, track-relative reads of 1..MH_OPTICAL_READ_SECTORS sectors.
	 * The frontend checks ranges. Audio must use the public PCM format.
	 * Never replace unreadable sectors with silence or partial success.
	 */
	enum mh_error_code (*read)(void *context, size_t track,
		unsigned long sector, unsigned int count, void *output,
		struct mh_error	*error_out);
	void			(*close)(void *context);
};

/*
 * Copies the TOC; takes ownership of context only on success.
 * The operations must outlive the disc. Failed creation preserves output.
 */
enum mh_error_code	mh_optical_disc_create(const struct mh_optical_ops *ops,
	void *context, const struct mh_optical_track *tracks, size_t count,
	struct mh_optical_disc **output, struct mh_error *error_out);

#endif
