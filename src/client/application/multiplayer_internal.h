#ifndef MH_CLIENT_MULTIPLAYER_INTERNAL_H
#define MH_CLIENT_MULTIPLAYER_INTERNAL_H

#include "client/application/application_internal.h"
#include "client/application/multiplayer.h"

enum mh_app_status	mh_multiplayer_step(struct mh_application *app);
void			mh_multiplayer_close(struct mh_application *app);

void	mh_multiplayer_suspend(struct mh_application *app);

#endif
