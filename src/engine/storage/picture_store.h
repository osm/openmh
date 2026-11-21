#ifndef MH_ENGINE_STORAGE_PICTURE_STORE_H
#define MH_ENGINE_STORAGE_PICTURE_STORE_H

#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_picture_store;
struct mh_data_directory;

/*
 * Adopt a prepared writable directory on success only. The provider must
 * outlive the store. No native paths or file handles are required. Errors
 * leave output and directory ownership unchanged; diagnostics survive close.
 */
enum mh_error_code	mh_picture_store_open_in(
	struct mh_data_directory *directory, struct mh_picture_store **output,
	struct mh_error *error_out);
void			mh_picture_store_close(struct mh_picture_store *store);

/*
 * ASCII-insensitive name probing, including links and nonregular entries.
 * Names are motorN/lobbyN plus a 1..8 lowercase alphanumeric extension.
 */
enum mh_error_code	mh_picture_store_exists(struct mh_picture_store *store,
	const char *name, int *present, struct mh_error *error_out);

/*
 * Publish nonempty encoded bytes up to 128 MiB; never replace an existing name.
 * Recheck case-insensitive availability immediately before publication.
 * Native providers flush/sync a private temporary before linking it. Local
 * success is separate from the host's durable COMMIT. Bytes remain borrowed
 * for this call only; no private file survives failure.
 */
enum mh_error_code	mh_picture_store_save(struct mh_picture_store *store,
	const char *name, const struct mh_file *picture,
	struct mh_error *error_out);

#endif
