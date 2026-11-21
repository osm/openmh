#ifndef MH_ASSETS_ILBM_H
#define MH_ASSETS_ILBM_H

#include <stddef.h>

#include "engine/assets/image.h"
#include "engine/core/error.h"

/*
 * Indexed ILBM, 1-8 planes, raw/ByteRun1, masks 0-3. No HAM/EHB.
 * RGBA rows run top to bottom. Caller frees rgba. Output is assigned only
 * on success. A decoded image is limited to 16 million pixels.
 */
enum mh_error_code	mh_image_ilbm_decode(const unsigned char *data,
	size_t size, struct mh_image *image, struct mh_error *error_out);

/*
 * Original hardware material conversion: index zero has alpha zero, all other
 * indices have alpha 255, independently of BMHD masking. RGB and bounded file
 * validation are shared with the ordinary decoder.
 */
enum mh_error_code	mh_image_ilbm_decode_texture(const unsigned char *data,
	size_t size, struct mh_image *image, struct mh_error *error_out);

/*
 * Preserve original indices and palette, without applying transparency or
 * light. Mask planes are still consumed/validated. Caller frees indices. Output
 * changes only on success; duplicate RGB entries keep their distinct source
 * indices.
 */
enum mh_error_code	mh_image_ilbm_decode_indexed(const unsigned char *data,
	size_t size, struct mh_indexed_image *image,
	struct mh_error *error_out);

/*
 * Ordinary image decode plus an owned palette. Both outputs are assigned only
 * on success; unused palette bytes are zero. Retains ordinary mask semantics.
 */
enum mh_error_code	mh_image_ilbm_decode_image_palette(
	const unsigned char *data, size_t size, struct mh_image *image,
	struct mh_image_palette *palette, struct mh_error *error_out);

/*
 * Texture decode with an owned palette copy for shared P8 page assembly.
 * Both outputs are assigned only on success. Unused palette bytes are zero.
 */
enum mh_error_code	mh_image_ilbm_decode_texture_palette(
	const unsigned char *data, size_t size, struct mh_image *image,
	struct mh_image_palette *palette, struct mh_error *error_out);

/*
 * Same bounded decoder with an optional shared RGB palette (1..256 colors).
 * Original particle atlases bind every frame to the first frame's palette.
 * NULL/zero uses the file palette. Source indices are checked against the
 * selected palette; mask semantics remain unchanged.
 */
enum mh_error_code	mh_image_ilbm_decode_palette(const unsigned char *data,
	size_t size, const unsigned char *palette, unsigned int colors,
	struct mh_image *image, struct mh_error *error_out);

/* Combined indexed palette/alpha selection for image decoder adapters. */
enum mh_error_code	mh_image_ilbm_decode_rgba(const unsigned char *data,
	size_t size, const struct mh_image_palette *palette, int index_zero,
	struct mh_image *output, struct mh_image_palette *palette_output,
	struct mh_error *error_out);

#endif
