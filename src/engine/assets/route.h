#ifndef MH_ASSETS_ROUTE_H
#define MH_ASSETS_ROUTE_H

#include "engine/core/error.h"
#include "engine/navigation/route.h"

/* Failure preserves output. Free successful output with mh_route_free. */
enum mh_error_code	mh_route_mot_parse(const unsigned char *text,
	size_t size, struct mh_route *route, struct mh_error *error_out);

/* Plain LWMO or TBL05-wrapped route; the same minimum and ownership apply. */
enum mh_error_code	mh_route_mot_decode(const unsigned char *data,
	size_t size, struct mh_route *route, struct mh_error *error_out);

#endif
