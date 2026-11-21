#ifndef MH_CLIENT_SETTINGS_CONTROLS_H
#define MH_CLIENT_SETTINGS_CONTROLS_H

#include "engine/core/error.h"
#include "engine/settings/config.h"
#include "engine/settings/control_layout.h"

struct mh_resource_source;
struct mh_user_data;
struct mh_control_catalog;
struct mh_control_store;

enum mh_control_storage {
	MH_CONTROL_SESSION,
	MH_CONTROL_EXISTING,
	MH_CONTROL_PERSISTENT
};

/*
 * Resolve installed profiles using resources. An explicit writable directory
 * must exist. With no explicit directory, SESSION ignores user profiles,
 * EXISTING uses the default user root if present, and PERSISTENT
 * creates it when missing. Providers must be ready before calling. The store
 * borrows resources/user_data through close; catalog/output stay unchanged on
 * failure. Session-only catalogs do not require user_data.
 */
enum mh_error_code	mh_control_profiles_open(
	const struct mh_resource_source *resources,
	const struct mh_user_data *user_data, const char *asset_root,
	const char *directory, enum mh_control_storage storage,
	const char *selection, struct mh_control_catalog *catalog,
	struct mh_control_store **output, struct mh_error *error_out);

/*
 * Read installed CLO files with optional user overrides, then apply the
 * selected profile. NULL directory uses the provider's default user
 * root if it exists; explicit directories must exist outside the assets.
 * NULL settings selects Keyboard without changing CFG state. Otherwise
 * update ControlName to the actual selected layout. No CLO writes. Binding
 * values are owned by the caller; all outputs are unchanged on failure.
 */
enum mh_error_code	mh_control_profiles_load_from(
	const struct mh_resource_source *resources,
	const struct mh_user_data *user_data, const char *asset_root,
	const char *directory, struct mh_settings *settings,
	struct mh_control_binding_set *output, struct mh_error *error_out);

enum mh_error_code	mh_control_profiles_restrict(
	struct mh_control_store *store, struct mh_control_catalog *catalog,
	struct mh_error *error_out);

#endif
