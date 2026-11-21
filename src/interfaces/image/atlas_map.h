#ifndef MH_IMAGE_ATLAS_MAP_H
#define MH_IMAGE_ATLAS_MAP_H

/*
 * Decoded atlas layout. Decoder output owns its entries and retains no encoded
 * bytes. Consumers borrow entries immutably, preserving registration order,
 * page indices, source/scaled dimensions and transposed rectangles. Authored
 * views may borrow ordinary arrays; do not pass borrowed views to owner
 * cleanup.
 */
struct mh_atlas_map_entry {
	/* Original 8.3 name; bytes after NUL may be stale padding. */
	char		name[13];

	/* Saved handle is retained, never used as a pointer. */
	unsigned long	page, saved_handle;
	unsigned long	x, y, transpose;
	unsigned long	source_width, source_height, scaled_width, scaled_height;
};

struct mh_atlas_map {
	struct mh_atlas_map_entry	*entries;
	unsigned long	count, pages, cutout_pages, width, height;
	float				scale[2];
};

#endif
