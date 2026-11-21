#ifndef MH_ENGINE_STORAGE_RECORDING_CATALOG_H
#define MH_ENGINE_STORAGE_RECORDING_CATALOG_H

#include <stddef.h>

#include "engine/core/error.h"
#include "engine/ui/menu_demo_catalog.h"

struct mh_recording_files;
struct mh_data_directory;
struct mh_file;
struct mh_resource_source;
struct mh_user_data;

struct mh_recording_catalog {
	struct mh_menu_demo_catalog	menu;
	struct mh_recording_files	*files;
};

/*
 * Prepared directory views, adopted only on success. NULL sources are empty;
 * non-NULL handles must be distinct (aliases of the same directory are fine).
 * Providers remain borrowed through catalog close. Installed entries never
 * become removable; an aliased user directory is not scanned twice. Failure
 * preserves both directories and output. No files are created while scanning.
 */
enum mh_error_code	mh_recording_catalog_read_in(
	struct mh_data_directory *installed, struct mh_data_directory *user,
	const char *saved, struct mh_recording_catalog *output,
	struct mh_error *error_out);

/*
 * Open source directories through the selected prepared views. NULL paths and
 * missing directories are empty sources. Providers are required only for the
 * sources requested and remain borrowed through catalog close. Never creates
 * directories. Other errors remain failures, with output unchanged.
 */
enum mh_error_code	mh_recording_catalog_read_from(
	const struct mh_resource_source *resources,
	const struct mh_user_data *data, const char *asset_root,
	const char *installed, const char *user, const char *saved,
	struct mh_recording_catalog *output, struct mh_error *error_out);

/* Compare source identity and exact filename across a catalog refresh. */
enum mh_error_code	mh_recording_catalog_same_entry(
	const struct mh_recording_catalog *first_catalog, unsigned int first,
	const struct mh_recording_catalog *second_catalog, unsigned int second,
	int *same, struct mh_error *error_out);

/* Bounded regular-file read from the selected entry's held source directory. */
enum mh_error_code	mh_recording_catalog_load(
	const struct mh_recording_catalog *catalog, unsigned int index,
	struct mh_file *output, struct mh_error *error_out);

void			mh_recording_catalog_close(
	struct mh_recording_catalog *catalog);
enum mh_error_code	mh_recording_catalog_path(
	const struct mh_recording_catalog *catalog, unsigned int index,
	char *path, size_t capacity, struct mh_error *error_out);

/*
 * Only regular user files outside the asset tree can be removed. Installed
 * aliases and symlinks remain read-only. Remove uses the retained directory
 * handle and rejects replaced entries; caller must refresh the catalog.
 */
int			mh_recording_catalog_removable(
	const struct mh_recording_catalog *catalog, unsigned int index);
enum mh_error_code	mh_recording_catalog_remove(
	struct mh_recording_catalog *catalog, unsigned int index,
	struct mh_error *error_out);

#endif
