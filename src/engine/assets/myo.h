#ifndef MH_ASSETS_MYO_H
#define MH_ASSETS_MYO_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/geometry/primitive.h"

struct mh_model;

enum mh_myo_block_index {
	MH_MYO_VERTICES,
	MH_MYO_PRIMITIVES,
	MH_MYO_PLANES,
	MH_MYO_NORMALS,
	MH_MYO_TEXTURE_REFS,
	MH_MYO_UV,
	MH_MYO_TEXTURE_NAMES,
	MH_MYO_BLOCK_COUNT
};

enum {
	MH_MYO_PRIMITIVE_TYPES = MH_GEOMETRY_FACE_TYPES
};

struct mh_myo_block {
	unsigned long	count;
	size_t		size;
	size_t		offset;
};

/*
 * A read-only view. The caller owns the byte buffer and keeps it alive.
 */
struct mh_myo {
	const unsigned char	*data;
	int			version;
	struct mh_myo_block	blocks[MH_MYO_BLOCK_COUNT];
	unsigned long		auxiliary_count;
	size_t			auxiliary_offset;
	unsigned long		primitive_types[MH_MYO_PRIMITIVE_TYPES];
	double			minimum[3];
	double			maximum[3];

	/* Header +0x68, original root-object sphere cull. */
	double			radius;
};

/*
 * Return MH_ERROR_NONE on success, or a failure code with optional static
 * diagnostic. Output is only assigned on success. The parser never changes the
 * input bytes.
 */
enum mh_error_code	mh_myo_parse(const unsigned char *data, size_t size,
	struct mh_myo *model, struct mh_error *error_out);

/* Return 0 for an invalid vertex index. */
int	mh_myo_vertex(const struct mh_myo *model, unsigned long index,
	double position[3]);

/* Only persistent type-2 auxiliary fields; no runtime pointers/history. */
int	mh_myo_halo(const struct mh_myo *model, unsigned long index,
	struct mh_geometry_halo *halo);

/*
 * Start cursor at zero. Each successful call advances it by one record.
 * Geometry validation is separate from structural parsing. Degenerate faces
 * remain structurally validated but must not be submitted for rendering.
 * All-NaN normals on valid faces use their finite plane direction; other
 * non-finite directions remain errors. The source bytes are never modified.
 * Types 4-7 use per-corner RGB and a flat normal, from hardware MYW worlds.
 * Model must come from mh_myo_parse or mh_myw_model.
 */
enum mh_error_code	mh_myo_next_face(const struct mh_myo *model,
	size_t *cursor, struct mh_geometry_face *face,
	struct mh_error *error_out);
const char	       *mh_myo_texture_name(const struct mh_myo *model,
	unsigned long index);

/* Owned geometry, independent of the input; unchanged output on failure. */
enum mh_error_code	mh_model_myo_decode(const unsigned char *data,
	size_t size, struct mh_model *model, struct mh_error *error_out);

#endif
