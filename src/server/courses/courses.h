#ifndef MH_SERVER_COURSES_H
#define MH_SERVER_COURSES_H

#include "engine/navigation/route.h"
#include "server/config/config.h"

struct mh_horn_source;
struct mh_navigation_source;
struct mh_resource_source;
struct mh_catalog_source;

struct mh_server_courses {
	struct mh_route	routes[MH_SERVER_TRACKS];
	unsigned int	count, selected;
};

/*
 * Discover missing Track entries from the mounted game's catalog. Explicit
 * entries keep their order and paths. An absent StartTrack selects the first
 * entry. Failure may leave partial configuration; do not start a server with
 * it.
 */
enum mh_error_code	mh_server_courses_configure(
	const struct mh_catalog_source *catalog,
	const struct mh_resource_source *resources, const char *root,
	struct mh_server_config *config, struct mh_error *error_out);

/* Course-only dedicated installations may omit either driver catalog. */
enum mh_error_code	mh_server_driver_catalog_load(
	const struct mh_catalog_source *catalog,
	const struct mh_horn_source *horn_source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_server_config *config, struct mh_error *error_out);

/*
 * Load every configured course relative to root; output is unchanged on error.
 * Navigation/resources are borrowed during loading. Encoded route reads are
 * limited to 16 MiB; returned routes own their keys independently.
 */
enum mh_error_code	mh_server_courses_load(
	const struct mh_navigation_source *navigation_source,
	const struct mh_resource_source *source, const char *root,
	const struct mh_server_config *config, struct mh_server_courses *output,
	unsigned int *failed_track, struct mh_error *error_out);
void			mh_server_courses_free(
	struct mh_server_courses *courses);

#endif
