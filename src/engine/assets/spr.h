#ifndef MH_ASSETS_SPR_H
#define MH_ASSETS_SPR_H

#include "engine/core/error.h"
#include "interfaces/sprite/pixels.h"

#define MH_SPR_NAME_BYTES 8

struct mh_spr_entry {
	char		name[MH_SPR_NAME_BYTES + 1];
	unsigned int	frames, width, height;
	size_t		offset, end;
};

struct mh_spr {
	/* Borrowed, immutable for the archive lifetime. */
	const unsigned char	*data;
	size_t			size, palette_offset;

	/* Owned sprite metadata. */
	struct mh_spr_entry	*entries;
	unsigned int		count;
};

/*
 * SPR archive and frame/program tables. Count <=4096, frames <=256,
 * dimensions <=4096 and <=16M pixels per frame; offsets must increase within
 * their enclosing region. Disk bytes are never executed. Output is unchanged
 * on error; caller supplies an empty output and frees it with
 * mh_sprite_spr_free.
 */
enum mh_error_code	mh_sprite_spr_read(const unsigned char *data,
	size_t size, struct mh_spr *output, struct mh_error *error_out);
void			mh_sprite_spr_free(struct mh_spr *archive);

/* Original exact eight-byte, zero-padded name matching. NULL if absent. */
const struct mh_spr_entry	*mh_sprite_spr_find(
	const struct mh_spr *archive, const char *name);

/*
 * Preserve palette indices for the menus' runtime palette changes.
 * Caller frees pixels on success. The same bounds/commands as below apply.
 */
enum mh_error_code	mh_sprite_spr_decode_indexed(
	const struct mh_spr *archive, unsigned int entry, unsigned int frame,
	struct mh_sprite_indexed *output, struct mh_error *error_out);

/*
 * Decode the indexed row programs to straight RGBA using the embedded RGB
 * palette. Untouched pixels are transparent; a written index zero is opaque.
 * Accept only immediate byte/word/dword stores, bounded base adjustments and
 * RET. No control transfer is followed. Parsed, unmodified archive required.
 * Caller frees image.rgba on success. Output unchanged on failure.
 */
enum mh_error_code	mh_sprite_spr_decode_rgba(const struct mh_spr *archive,
	unsigned int entry, unsigned int frame, struct mh_image *output,
	struct mh_error *error_out);

/*
 * Software HUD literal-color programs. Caller supplies the layout: columns
 * is 1 for the original meter, 0 for ordinary sprites. Row header widths are
 * bytes (must be even); column headers store height then width. No format or
 * layout is inferred from pixel values. Output retains the original 16 bits
 * and separate coverage. Same immutable archive/atomic output ownership.
 */
enum mh_error_code	mh_sprite_spr_decode_literal(
	const struct mh_spr *archive, unsigned int entry, unsigned int frame,
	int columns, struct mh_sprite_words *output,
	struct mh_error *error_out);

/*
 * RGB555 (green_bits=5) or RGB565 (6), expanded by high-bit replication.
 * A written zero word is opaque black; bit 15 in RGB555 is not alpha.
 */
enum mh_error_code	mh_sprite_spr_decode_literal_rgba(
	const struct mh_spr *archive, unsigned int entry, unsigned int frame,
	int columns, unsigned int green_bits, struct mh_image *output,
	struct mh_error *error_out);

#endif
