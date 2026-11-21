#ifndef MH_ASSETS_SPR_FORMAT_H
#define MH_ASSETS_SPR_FORMAT_H

/* Private disk layout shared by archive validation and frame decoding. */
enum {
	SPR_HEADER_BYTES = 4,
	SPR_ENTRY_BYTES = 12,
	/* Stored row dimensions; literal columns swap them after validation. */
	SPR_WIDTH_OFFSET = 4,
	SPR_HEIGHT_OFFSET = 8,
	SPR_FRAME_TABLE_OFFSET = 12,
	SPR_OFFSET_BYTES = 4,
	SPR_PALETTE_BYTES = 768, /* 256 RGB entries. */
	SPR_ENTRY_LIMIT = 4096,
	SPR_FRAME_LIMIT = 256,
	SPR_DIMENSION_LIMIT = 4096
};

#endif
