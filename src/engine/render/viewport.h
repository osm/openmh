#ifndef MH_VIEWPORT_H
#define MH_VIEWPORT_H

/*
 * Default inspection viewport. Sized race targets use the native bounds
 * below; these are resource limits, not original device mode limits.
 */
#define MH_VIEW_WIDTH 960
#define MH_VIEW_HEIGHT 640
#define MH_VIEW_MIN_WIDTH 320
#define MH_VIEW_MIN_HEIGHT 200
#define MH_VIEW_MAX_WIDTH 4096
#define MH_VIEW_MAX_HEIGHT 2160

/* Original packed software texture spans use a ten-bit pixel count. */
#define MH_SOFTWARE_VIEW_MAX_WIDTH 1023

#endif
