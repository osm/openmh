#ifndef MH_ENGINE_STORAGE_DEMO_FILE_H
#define MH_ENGINE_STORAGE_DEMO_FILE_H

#include "engine/core/error.h"
#include "engine/demo/record.h"

struct mh_demo_file;
struct mh_data_directory;

/*
 * Portable writer on a prepared directory. Adopts directory only on success;
 * the provider remains borrowed through finish/abort. Filename is an immediate
 * basename. ignore_case selects DOS-style collision checks for named user
 * takes. Private bytes use the selected stream provider; publication and host
 * COMMIT are separate. Failed open preserves output and caller directory
 * ownership. Failure diagnostics remain valid after closing that directory.
 */
enum mh_error_code	mh_demo_file_open_in(
	struct mh_data_directory *directory, const char *filename,
	int ignore_case, const struct mh_demo *header,
	struct mh_demo_file **output, struct mh_error *error_out);

/*
 * Prepare a fresh private stream for a restarted race, then discard the old
 * one. Only a writable take may restart. Failure leaves the old take intact.
 * Uses the same held directory, filename and protection policy.
 */
enum mh_error_code	mh_demo_file_restart(struct mh_demo_file *file,
	const struct mh_demo *header, struct mh_error *error_out);
enum mh_error_code	mh_demo_file_append(struct mh_demo_file *file,
	const struct mh_demo_record_event *event, struct mh_error *error_out);

/* True after a provider append failure, not invalid input or format bounds. */
int	mh_demo_file_write_failed(const struct mh_demo_file *file);

/*
 * Close and backfill a private take without publishing it. Does not consume
 * the object; after an error only abort is allowed. Successful seal permits
 * publication or a frontend name/overwrite prompt after the race is freed.
 */
enum mh_error_code	mh_demo_file_seal(struct mh_demo_file *file,
	const struct mh_demo *header, size_t frame_count,
	struct mh_error *error_out);

/*
 * Publish a sealed take at its original, exclusive destination. Does not
 * consume the object. A failed publication can be retried; always abort
 * afterwards to release the private file/handles.
 */
enum mh_error_code	mh_demo_file_publish(struct mh_demo_file *file,
	struct mh_error *error_out);

/*
 * Frontend save: choose a 1..23 byte basename, append .MDE, and probe the
 * take's own directory case-insensitively. Reject paths into asset_root,
 * links, nonregular entries and ambiguous case aliases. Output is unchanged
 * on error and any old probe is invalidated. The original editor uses byte
 * strings; high bytes are retained verbatim in provider names.
 */
enum mh_error_code	mh_demo_file_probe(struct mh_demo_file *file,
	const char *asset_root, const unsigned char *name, int *exists,
	struct mh_error *error_out);

/*
 * Publish the last successful probe. Replacement requires overwrite=1 and
 * a still-matching regular file. Revalidate directory, case aliases and file
 * identity immediately before atomic publication. No object is consumed;
 * failures retain the take for another name/probe.
 */
enum mh_error_code	mh_demo_file_save(struct mh_demo_file *file,
	int overwrite, struct mh_error *error_out);

/*
 * Consumes the file on success or error. Validates frame count/unchanged
 * metadata, backfills checkpoint tables and flushes before publication.
 * Fewer than 64 frames are rejected, preserving the original loader limit.
 */
enum mh_error_code	mh_demo_file_finish(struct mh_demo_file *file,
	const struct mh_demo *header, size_t frame_count,
	struct mh_error *error_out);
void			mh_demo_file_abort(struct mh_demo_file *file);

#endif
