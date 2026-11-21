#ifndef MH_DATA_DIRECTORY_H
#define MH_DATA_DIRECTORY_H

#include "engine/core/file.h"
#include "interfaces/storage/directory.h"

struct mh_data_directory;
struct mh_data_file;

/*
 * A prepared directory held through catalog use. Providers retain
 * directory identity; a renamed/replaced native directory cannot silently
 * redirect writes. Names are immediate basenames, never paths. Calls are
 * serialized. Close files before their directory, then release its provider.
 * Listing borrows names during callbacks and can partially visit on failure.
 */
enum mh_error_code	mh_data_directory_validate(
	struct mh_data_directory *directory, struct mh_error *error_out);
enum mh_error_code	mh_data_directory_list(
	struct mh_data_directory *directory, mh_directory_visit visit,
	void *context, struct mh_error *error_out);

/*
 * Set/replace resource-tree protection after checking the held directory.
 * Failure retains previous protection. Disjoint namespaces may ignore the
 * root; native providers resolve and retain it. Missing support: UNSUPPORTED.
 */
enum mh_error_code	mh_data_directory_protect(
	struct mh_data_directory *directory, const char *resource_root,
	struct mh_error *error_out);

/*
 * Retain an existing regular file's identity without reading its contents.
 * Used when replacement does not depend on previous bytes. Native symlinks
 * are rejected; read permission and allocation proportional to size are not
 * required. Missing files return NOT_FOUND; errors preserve output.
 */
enum mh_error_code	mh_data_directory_open_file(
	struct mh_data_directory *directory, const char *name,
	struct mh_data_file **output, struct mh_error *error_out);

/*
 * Read transfers malloc-owned bytes and a file identity retained for later
 * validation, replacement or deletion. Errors leave both outputs unchanged.
 * Files must be regular and unchanged during reading; native symlinks are
 * rejected. limit bounds bytes.
 * Missing files return NOT_FOUND so catalogs can apply their missing-file rule.
 */
enum mh_error_code	mh_data_directory_read(
	struct mh_data_directory *directory, const char *name, size_t limit,
	struct mh_file *bytes, struct mh_data_file **output,
	struct mh_error *error_out);

/*
 * Optional policy check immediately before publication, after preparing bytes.
 * It may validate/list/read the directory, but must not mutate or close it.
 * For example, a catalog can recheck its capacity and case-folded name rules.
 */
typedef enum mh_error_code (*mh_data_publish_check)(void *context,
	struct mh_data_directory *directory, struct mh_error *error_out);

/*
 * Exclusive atomic creation: failure leaves no published file and preserves
 * output. Successful creation returns its retained identity. All allocation
 * precedes publication. Native files are flushed/synced before publication;
 * directory durability still belongs to the host's separate COMMIT service.
 */
enum mh_error_code	mh_data_directory_create(
	struct mh_data_directory *directory, const char *name,
	const void *bytes, size_t size, mh_data_publish_check check,
	void *context, struct mh_data_file **output,
	struct mh_error *error_out);

/* Validate the loaded identity without changing it, for catalog preflight. */
enum mh_error_code	mh_data_file_validate(struct mh_data_file *file,
	struct mh_error *error_out);

/*
 * Accept content changes only if this is still the same regular directory
 * entry, then update this handle's content revision. Other handles are not
 * changed. Used by overwrite confirmation that does not depend on old bytes.
 * Native identity is device/inode, not size/mtime. Replaced/missing entries
 * fail without changing the handle. Missing support returns UNSUPPORTED.
 */
enum mh_error_code	mh_data_file_refresh(struct mh_data_file *file,
	struct mh_error *error_out);

/*
 * Replace only the still-current loaded file. Success updates this handle's
 * identity; other handles retain their earlier identities. Failure preserves
 * the file and this handle. The optional check has create's prepublication
 * semantics. Native checks precede rename; this is not a cross-process lock.
 * Bytes are consumed synchronously; durability remains a separate COMMIT.
 */
enum mh_error_code	mh_data_file_replace(struct mh_data_file *file,
	const void *bytes, size_t size, mh_data_publish_check check,
	void *context, struct mh_error *error_out);

/* Reject a missing or changed file; success does not release the handle. */
enum mh_error_code	mh_data_file_remove(struct mh_data_file *file,
	struct mh_error *error_out);

/*
 * Optional catalog inspection. path is borrowed until directory close; NULL
 * means unavailable. same compares provider directory identities, including
 * native aliases. Different implementations are distinct. Errors preserve
 * outputs. inspect succeeds with a NULL file for nonregular visible entries;
 * regular files return an owned token plus a removal-policy flag. Missing
 * entries report NOT_FOUND. It does not read contents or follow native links.
 */
const char	       *mh_data_directory_path(
	const struct mh_data_directory *directory);
enum mh_error_code	mh_data_directory_same(
	struct mh_data_directory *directory, struct mh_data_directory *other,
	int *same, struct mh_error *error_out);
enum mh_error_code	mh_data_directory_inspect(
	struct mh_data_directory *directory, const char *name,
	struct mh_data_file **file, int *removable, struct mh_error *error_out);

/*
 * Catalog deletion: retain the selected entry identity while accepting edits
 * to its contents. Reject a replacement or nonregular entry. Native providers
 * use the held original directory even after rename, never a replacement at
 * its former path. Only entries captured from an allowed user directory can
 * be removed. Ordinary mh_data_file_remove retains its stricter save checks.
 */
enum mh_error_code	mh_data_file_remove_entry(struct mh_data_file *file,
	struct mh_error *error_out);
void			mh_data_file_close(struct mh_data_file *file);
void			mh_data_directory_close(
	struct mh_data_directory *directory);

#endif
