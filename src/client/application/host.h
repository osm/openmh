#ifndef MH_CLIENT_HOST_H
#define MH_CLIENT_HOST_H

#include "engine/core/error.h"

/*
 * Optional host lifecycle support, called between application steps on the
 * host thread. The callback/context are borrowed through application close.
 *
 * Successful suspension quiesces audio output/service callbacks and external
 * activity that cannot run in the background. It retains resources and allows
 * their normal close while suspended. The host owns network disconnect policy.
 * Resume makes presentation and input usable again, discarding stale input.
 * A lost surface/device must be restored here or reported as a failure; the
 * application never draws after a failed resume. Independently timed video or
 * network services must handle their own interruption time in this callback.
 *
 * Failures leave the previous service state intact (or return UNSUPPORTED
 * before changing anything). The callback does not reenter the application.
 * The callback must not wait for external storage/network completion.
 * Application/game time is frozen separately by the application. These hooks
 * do not serialize an in-progress race or promise background execution time.
 */
struct mh_app_lifecycle {
	void		*context;
	enum mh_error_code (*set_suspended)(
		void	*context, int suspended, struct mh_error *error_out);
};

#endif
