#ifndef MH_GEOMETRY_MODEL_H
#define MH_GEOMETRY_MODEL_H

#include <stddef.h>

#include "interfaces/geometry/primitive.h"

/*
 * Decoded model values. Decoder results own separately malloc-allocated arrays
 * and texture names; consumers borrow them immutably until the owner releases
 * them. Hand-authored views may borrow arrays instead and must not be passed to
 * owner cleanup. No input bytes or decoder state are retained by a decoder
 * result. Primitive and texture order is stable.
 * Faces include their corner positions; vertex_count also counts unreferenced
 * source vertices used when deriving the bounds. Radius is the supplied root
 * culling sphere, not a sphere recomputed from those bounds.
 */
struct mh_model {
	double			minimum[3];
	double			maximum[3];
	double			radius;
	size_t			vertex_count;
	size_t			face_count;
	size_t			texture_count;
	size_t			halo_count;
	struct mh_geometry_face	*faces;
	char			**textures;
	struct mh_geometry_halo	*halos;
};

#endif
