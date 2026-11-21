#ifndef MH_ASSETS_FONT_H
#define MH_ASSETS_FONT_H

#include "engine/core/error.h"
#include "interfaces/font/glyphs.h"

struct mh_image_decoder_provider;

/* Output unchanged on failure; caller frees bytes on success. */
enum mh_error_code	mh_font_fnt_decode_program(const unsigned char *data,
	size_t size, struct mh_font_program *output,
	struct mh_error *error_out);

/*
 * Decode FONT1/FONT2 and their small variants as byte drawing programs.
 * FONT2 retains byte widths here; callers using word pixels must account
 * for that separately. Recognizes only row advances, fixed foreground spans,
 * and the terminal jump marker.
 * Never executes file bytes. Height/advance <=64; <=4096 bytes per glyph.
 * Output unchanged on failure; caller frees image.rgba on success.
 */
enum mh_error_code	mh_font_fnt_decode(const unsigned char *data,
	size_t size, struct mh_font *output, struct mh_error *error_out);

/*
 * Only palette index 255 covers pixels in MYFONT.IFF. Advances include one
 * trailing pixel. Space uses half a cell plus one. The caller owns image.rgba
 * on success. Output is unchanged on failure.
 */
enum mh_error_code	mh_font_hardware_decode_from(
	const struct mh_image_decoder_provider *decoder,
	const unsigned char *data, size_t size, struct mh_font *output,
	struct mh_error *error_out);

#endif
