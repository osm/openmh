#ifndef MH_ENGINE_STORAGE_ADDRESS_STORE_H
#define MH_ENGINE_STORAGE_ADDRESS_STORE_H

#include "engine/core/error.h"
#include "engine/settings/address_book.h"

struct mh_address_store;
struct mh_data_directory;

/*
 * Portable NPC catalog over a prepared directory. Takes directory ownership
 * on success only; close releases file identities before the directory.
 * Reload failure retains the previous catalog and identities. Load regular NPC
 * files in ASCII-folded filename order, retaining TCP entries only, then sort
 * descriptions with mh_address_book_sort(). Reserve other provider filenames.
 * Limits are 4095 files, 1 MiB each and 32 MiB total;
 * reject ambiguous names. The returned book is borrowed until mutation or
 * close.
 */
enum mh_error_code	mh_address_store_open_in(
	struct mh_data_directory *directory, struct mh_address_store **output,
	struct mh_error *error_out);

const struct mh_address_book	*mh_address_store_book(
	const struct mh_address_store *store);

/*
 * Reread the same directory at frontend reentry. Success replaces all borrowed
 * entries and their deletion identities. Failure leaves the loaded catalog
 * usable. This never writes NPC files.
 */
enum mh_error_code	mh_address_store_reload(struct mh_address_store *store,
	struct mh_error *error_out);

/*
 * Addition prepares all memory before publishing a private, synced file
 * under an exclusive filename. Removal checks the loaded file's identity,
 * size and timestamps before unlinking it. Failure preserves the catalog;
 * failed addition leaves no published file. Calls require serialization.
 * Directory entries are not fsynced; no power-loss durability is promised.
 */
enum mh_error_code	mh_address_store_add(struct mh_address_store *store,
	const char *name, const char *ip, struct mh_error *error_out);
enum mh_error_code	mh_address_store_remove(struct mh_address_store *store,
	unsigned int index, struct mh_error *error_out);
void			mh_address_store_close(struct mh_address_store *store);

#endif
