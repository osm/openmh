#ifndef MH_UNSHIELD_INTERNAL_H
#define MH_UNSHIELD_INTERNAL_H

#include <libunshield.h>

#include "interfaces/archive/archive_backend.h"

struct cabinet_member {
	int			index;
	struct mh_archive_entry	entry;
};

struct cabinet {
	Unshield		*reader;
	const unsigned char	*bytes;
	size_t			size;
	struct cabinet_member	*members;
	size_t			count;
	unsigned char		*output;
	size_t			output_size, written;
};

extern const UnshieldIoCallbacks mh_unshield_memory_io;

#endif
