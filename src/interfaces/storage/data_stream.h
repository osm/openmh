#ifndef MH_DATA_STREAM_H
#define MH_DATA_STREAM_H

#include "interfaces/storage/data_directory.h"

struct mh_data_stream;

/*
 * Private sequential output in a prepared directory. No final destination is
 * visible until publication. The stream borrows its directory/provider: close
 * it before either owner. Providers may use temporary files or private memory;
 * callers need no file descriptors, seekable FILE streams or native threads.
 * Missing support returns UNSUPPORTED; failed open leaves output unchanged.
 */
enum mh_error_code	mh_data_stream_open(struct mh_data_directory *directory,
	struct mh_data_stream **output, struct mh_error *error_out);

/*
 * Append bytes or patch an existing range without changing its length. NULL
 * bytes are valid only for size zero. Dispatcher argument/range checks leave
 * a stream usable on failure. A provider write failure may be partial; then
 * only close is allowed (including provider-specific capacity/range failures).
 */
enum mh_error_code	mh_data_stream_append(struct mh_data_stream *stream,
	const void *bytes, size_t size, struct mh_error *error_out);
enum mh_error_code	mh_data_stream_write_at(struct mh_data_stream *stream,
	size_t offset, const void *bytes, size_t size,
	struct mh_error *error_out);
size_t			mh_data_stream_size(
	const struct mh_data_stream *stream);

/*
 * Finish private bytes, checking their length. After success only publication
 * or close is allowed; after failure only close. Native providers flush/sync
 * here. Durable host COMMIT remains separate from local publication.
 */
enum mh_error_code	mh_data_stream_seal(struct mh_data_stream *stream,
	struct mh_error *error_out);

/*
 * Publish once: exclusive creation, or replacement of a still-current file
 * from the same held directory. The optional policy check runs immediately
 * before publication, as with whole-file writes. Failure preserves destination,
 * file identity and output and retains the sealed stream for another attempt.
 * Successful replacement updates the supplied identity. Always close afterward.
 */
enum mh_error_code	mh_data_stream_create(struct mh_data_stream *stream,
	const char *name, mh_data_publish_check check, void *context,
	struct mh_data_file **output, struct mh_error *error_out);
enum mh_error_code	mh_data_stream_replace(struct mh_data_stream *stream,
	struct mh_data_file *file, mh_data_publish_check check, void *context,
	struct mh_error *error_out);
void			mh_data_stream_close(struct mh_data_stream *stream);

#endif
