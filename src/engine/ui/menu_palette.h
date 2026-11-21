#ifndef MH_MENU_PALETTE_H
#define MH_MENU_PALETTE_H

#include "engine/core/error.h"
struct mh_sprite_collection;

#include "interfaces/image/pixels.h"

#define MH_MENU_PALETTE_BYTES MH_IMAGE_PALETTE_BYTES
#define MH_MENU_PALETTE_SLICE_BYTES 192

enum mh_error_code	mh_menu_palette_load(
	const struct mh_sprite_collection *collection,
	unsigned char output[MH_MENU_PALETTE_BYTES],
	struct mh_error *error_out);

/*
 * All arrays are RGB byte triples. Background provides indices 192..255;
 * optional preview provides 128..191. Frame fade is 0..1. Fixed font colors
 * remain set while preview and index 255 change.
 */
enum mh_error_code	mh_menu_palette_base(
	const unsigned char sprites[MH_MENU_PALETTE_BYTES],
	const unsigned char background[MH_MENU_PALETTE_SLICE_BYTES],
	unsigned char output[MH_MENU_PALETTE_BYTES],
	struct mh_error *error_out);
enum mh_error_code	mh_menu_palette_frame(
	const unsigned char sprites[MH_MENU_PALETTE_BYTES],
	const unsigned char background[MH_MENU_PALETTE_SLICE_BYTES],
	const unsigned char *preview, float fade,
	unsigned char output[MH_MENU_PALETTE_BYTES],
	struct mh_error *error_out);

/*
 * Set base index 6 from the low 24 bits of the original 32-bit player color,
 * then snapshot the submitted RGB palette. Call before page draw/input; later
 * base edits belong to the next frame. The original submits base, not its
 * separately computed flicker scratch. Base and presented must be separate
 * arrays; errors leave both unchanged.
 */
enum mh_error_code	mh_menu_palette_submit(
	unsigned char base[MH_MENU_PALETTE_BYTES], unsigned long player_color,
	unsigned char presented[MH_MENU_PALETTE_BYTES],
	struct mh_error *error_out);

#endif
