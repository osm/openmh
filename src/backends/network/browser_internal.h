#ifndef MH_BACKENDS_NETWORK_BROWSER_INTERNAL_H
#define MH_BACKENDS_NETWORK_BROWSER_INTERNAL_H

#include "backends/network/browser_provider.h"

enum {
	BROWSER_IDLE,
	BROWSER_CONNECTING,
	BROWSER_CONNECTED,
	BROWSER_CLOSED,
	BROWSER_SERVER_PEER = 1,
	BROWSER_POLL_MESSAGES = 64
};

struct mh_browser_ice_copy {
	char	url[MH_BROWSER_ICE_URL_BYTES];
	char	username[MH_BROWSER_ICE_CREDENTIAL_BYTES];
	char	credential[MH_BROWSER_ICE_CREDENTIAL_BYTES];
};

struct mh_browser_target {
	char				url[MH_BROWSER_SIGNAL_URL_BYTES];
	struct mh_browser_ice_copy	ice[MH_BROWSER_ICE_SERVERS];
	unsigned int			ice_count;
	int				relay_only;
};

/* Synchronous copying bridge; no C pointer is retained by JavaScript. */
int		mh_browser_net_open(void);
void		mh_browser_net_close(int handle);
void		mh_browser_net_ice(int handle, const char *url,
	const char *username, const char *credential);
void		mh_browser_net_connect(int handle, const char *url,
	int relay_only);
int		mh_browser_net_state(int handle);
int		mh_browser_net_send(int handle, const unsigned char *data,
	size_t size);
size_t		mh_browser_net_receive(int handle, unsigned char *data,
	size_t capacity);
size_t		mh_browser_net_reason(int handle, unsigned char *data,
	size_t capacity);
void		mh_browser_net_disconnect(int handle);
double		mh_browser_net_now(void);
unsigned long	mh_browser_net_ticks(void);

#endif
