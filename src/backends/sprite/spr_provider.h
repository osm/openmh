#ifndef MH_BACKENDS_SPRITE_SPR_PROVIDER_H
#define MH_BACKENDS_SPRITE_SPR_PROVIDER_H

#include "interfaces/sprite/collection.h"

/* Original SPR collections; includes the software HUD's column-coded meter. */
const struct mh_sprite_source_provider	*mh_sprite_spr_source(void);

/* Copies original bytes; frames and collection have independent ownership. */
enum mh_error_code	mh_sprite_spr_open(const unsigned char *data,
	size_t size, enum mh_picture_format format,
	struct mh_sprite_collection **output, struct mh_error *error_out);

#endif
