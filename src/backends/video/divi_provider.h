#ifndef MH_BACKENDS_VIDEO_DIVI_PROVIDER_H
#define MH_BACKENDS_VIDEO_DIVI_PROVIDER_H

struct mh_video_source_provider;

/*
 * Process-lifetime source for original DIVI collections and standalone clips.
 */
const struct mh_video_source_provider	*mh_video_divi_source(void);

#endif
