#ifndef MH_CLIENT_HOSTS_SDL_CLIPBOARD_H
#define MH_CLIENT_HOSTS_SDL_CLIPBOARD_H

struct mh_clipboard_provider;

/* NULL when the selected host omits clipboard support. */
const struct mh_clipboard_provider	*mh_host_sdl_clipboard(void);

#endif
