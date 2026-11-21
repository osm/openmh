#ifndef MH_CLIP_CODES_H
#define MH_CLIP_CODES_H

/* Original projected-point outcodes; viewport Y increases downward. */
enum mh_clip_code {
	MH_CLIP_LEFT = 1,
	MH_CLIP_RIGHT = 2,
	MH_CLIP_TOP = 4,
	MH_CLIP_BOTTOM = 8,
	MH_CLIP_VIEWPORT = 15,
	MH_CLIP_NEAR = 16,
	MH_CLIP_FAR = 32,
	MH_CLIP_ALL = 63
};

#endif
