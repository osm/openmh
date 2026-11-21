#ifndef MH_RETRO_PARTS_H
#define MH_RETRO_PARTS_H

#include "interfaces/retro/data.h"

/* Copy a valid part tag between a complete asset set and a provider result. */
void	mh_retro_part_store(struct mh_retro_assets *assets,
	enum mh_retro_part part, const union mh_retro_part_data *data);
void	mh_retro_part_copy(const struct mh_retro_assets *assets,
	enum mh_retro_part part, union mh_retro_part_data *data);

#endif
