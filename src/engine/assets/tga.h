#ifndef MH_ASSETS_TGA_H
#define MH_ASSETS_TGA_H

#include "engine/assets/image.h"
#include "engine/core/error.h"
#include "engine/core/file.h"
#include "interfaces/image/pixels.h"

#define MH_TGA_MAX_PIXELS 16000000UL

enum {
	MH_TGA_HEADER_SIZE = 18,
	MH_TGA_MAX_COLORS = 256
};

/*
 * Uncompressed indexed (8-bit), BGR555 (16-bit), BGR/BGRA (24/32-bit),
 * and grayscale (8-bit). BGR555 uses bit 15 only with one attribute bit.
 * Top-down RGBA output, at most 16 million pixels. Optional magenta
 * key is a sprite policy, not an implicit property of the file format.
 * Caller frees rgba; output is unchanged on failure.
 */
enum mh_error_code	mh_image_tga_decode(const unsigned char *data,
	size_t size, int magenta_key, struct mh_image *output,
	struct mh_error *error_out);

/*
 * Preserve top-down palette indices and RGB entries, including duplicate
 * colors. Palette origins do not renumber indices; unused leading entries are
 * zero and colors includes those entries. Alpha/key policy is deliberately not
 * applied. A palette's alpha bytes are discarded by this RGB-only view, as with
 * ILBM indexed decoding. True-color/grayscale inputs report UNSUPPORTED rather
 * than quantizing. Caller frees indices; output is unchanged on failure.
 */
enum mh_error_code	mh_image_tga_decode_indexed(const unsigned char *data,
	size_t size, struct mh_indexed_image *output,
	struct mh_error *error_out);

/*
 * Encode TGA bytes for surface formats 1/3/4. RGB565 loses its low green bit;
 * RGB555 words are copied verbatim. RGBA8 is truncated to RGB555, ignoring
 * alpha, as with hardware capture. Rows are bottom-up. Indexed output
 * deliberately retains the unusual header (palette depth 8, count
 * 256+(palette_count&255), descriptor 8). It is not a general TGA encoder.
 * Pitch is raised to the packed row length. Bounds: 1..65535 dimensions, <=16M
 * pixels, 1..256 palette entries. Caller frees output->data. Output is
 * unchanged on error.
 */
enum mh_error_code	mh_image_tga_encode(const struct mh_picture *picture,
	struct mh_file *output, struct mh_error *error_out);

/*
 * Encode a standard top-down, 8-bit indexed TGA with all 256 RGB entries.
 * Preserves indices and unused palette entries. No quantization or alpha.
 * Dimensions obey the same limits as the capture encoder; size must cover
 * every pixel. Success transfers malloc-owned bytes; failure preserves output.
 */
enum mh_error_code	mh_image_tga_encode_indexed(
	const struct mh_indexed_image *image, struct mh_file *output,
	struct mh_error *error_out);

/*
 * Results-specific TGA profile: fixed 18-byte header and 90x56 gray payload,
 * vertical-origin bit only. Other metadata is ignored. Returns top-down,
 * unquantized opaque gray RGBA. Caller frees rgba. No missing-file fallback or
 * results palette preparation is applied here.
 */
enum mh_error_code	mh_image_tga_decode_portrait(const unsigned char *data,
	size_t size, struct mh_image *output, struct mh_error *error_out);

/*
 * Original menu preview view: fixed offsets retain the 191-color file's
 * three-byte displacement and palette entry 191 read from pixel bytes.
 * Returns owned 324x269 indices addressing the 256-entry menu palette.
 * Only RGB entries 128..191 are supplied; other entries are zero. This is not
 * ordinary TGA decoding. Caller frees indices; failure preserves output.
 */
enum mh_error_code	mh_image_tga_decode_thumbnail(const unsigned char *data,
	size_t size, struct mh_indexed_image *output,
	struct mh_error *error_out);

#endif
