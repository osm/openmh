#ifndef MH_GEOMETRY_DECODER_BACKEND_H
#define MH_GEOMETRY_DECODER_BACKEND_H

#include "interfaces/geometry/decoder.h"

/*
 * Validate counts, references, finite geometry/bounds and material modes before
 * publishing output. Keep face/material/texture/halo order and units. Arrays
 * follow model.h/world.h ownership; failure frees partial state. Extensions
 * are NULL or 1..16 lowercase letters/digits, independently for each family.
 * World providers also validate grid/cell spans and acyclic light links. Face
 * identities remain stable and reserve their successor for a quad's second
 * half; they are opaque identifiers, never a consumer's disk offsets.
 */
struct mh_model_decoder {
	void			*context;
	const char		*extension;
	enum mh_error_code (*decode)(void *context, const unsigned char *data,
		size_t size, struct mh_model *output,
		struct mh_error	*error_out);
};

struct mh_world_decoder {
	void			*context;
	const char		*extension;
	enum mh_error_code (*decode)(void *context, const unsigned char *data,
		size_t size, struct mh_world *output,
		struct mh_error	*error_out);
};

#endif
