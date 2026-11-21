#ifndef MH_BROWSER_MULTIPLAYER_API_H
#define MH_BROWSER_MULTIPLAYER_API_H

struct mh_application;
struct mh_race_request;

/* Borrow application/settings until detach; all exports run on its thread. */
void		 mh_browser_multiplayer_attach(struct mh_application *app,
	const struct mh_race_request *settings);
void		 mh_browser_multiplayer_detach(void);
int		 mh_browser_multiplayer_available(void);
int		 mh_browser_multiplayer_configure(const char *url,
	int relay_only);
int		 mh_browser_multiplayer_ice(const char *url,
	const char *username, const char *credential);
int		 mh_browser_multiplayer_join(const char *password);
int		 mh_browser_multiplayer_leave(void);
int		 mh_browser_multiplayer_return(void);
int		 mh_browser_multiplayer_player(const char *name,
	const char *team, unsigned long color);
int		 mh_browser_multiplayer_show_fps(int enabled);
int		 mh_browser_multiplayer_chat(const char *text);
int		 mh_browser_multiplayer_ready(int ready);
int		 mh_browser_multiplayer_toggle_ready(void);
const char	*mh_browser_multiplayer_error(void);

/* Refresh owns a copy; getters never expose live engine structures. */
int		 mh_browser_multiplayer_refresh(void);
double		 mh_browser_multiplayer_number(const char *field,
	unsigned int index);
const char	*mh_browser_multiplayer_text(const char *field,
	unsigned int index);

#endif
