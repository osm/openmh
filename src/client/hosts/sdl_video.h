#ifndef MH_CLIENT_HOSTS_SDL_VIDEO_H
#define MH_CLIENT_HOSTS_SDL_VIDEO_H

struct mh_video_provider;

/* Borrowed optional playback service, retained until host process exit. */
const struct mh_video_provider	*mh_host_sdl_videos(void);

#endif
