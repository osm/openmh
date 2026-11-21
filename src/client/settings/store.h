#ifndef MH_CLIENT_SETTINGS_STORE_H
#define MH_CLIENT_SETTINGS_STORE_H

#include "engine/core/error.h"
#include "engine/settings/config.h"

struct mh_horn_source;

struct mh_catalog_source;
struct mh_settings_store;
struct mh_resource_source;
struct mh_user_data;

/*
 * Copy installed car/track and horn catalogs through the selected sources
 * during open, then use user_data for settings reads/reloads/saves. Borrow
 * user_data and the renderer name array/strings through close; copy the
 * renderer catalog descriptor. NULL path
 * resolves motorhead.cfg under the provider's default root. Keep resources
 * ready during open; the store retains neither catalog/horn sources nor
 * resource pointers afterward.
 * Failed opens preserve the caller's random state and output.
 */
enum mh_error_code			 mh_settings_store_open_with_sources(
	const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources,
	const struct mh_user_data *user_data,
	const struct mh_settings_names *renderers, const char *asset_root,
	const char *path, const struct mh_settings *defaults,
	unsigned long *random, struct mh_settings_store **output,
	struct mh_error *error_out);
struct mh_settings			*mh_settings_store_values(
	struct mh_settings_store *store);
const struct mh_settings_catalog	*mh_settings_store_catalog(
	const struct mh_settings_store *store);
const char				*mh_settings_store_path(
	const struct mh_settings_store *store);

/*
 * Only changed settings or a missing file are written. Successful saves
 * establish a new baseline without a lossy encode/read cycle.
 */
enum mh_error_code	mh_settings_store_save(struct mh_settings_store *store,
	struct mh_error *error_out);

/*
 * Reinitialize from the caller's opening defaults and read the current
 * file, as on original frontend reentry. Successful reads replace values
 * and the save baseline; no write occurs. Errors preserve store and RNG.
 * Missing files restore defaults and mark the baseline absent.
 */
enum mh_error_code	mh_settings_store_reload(
	struct mh_settings_store *store, unsigned long *random,
	struct mh_error *error_out);
void			mh_settings_store_close(
	struct mh_settings_store *store);

#endif
