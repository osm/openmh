#ifndef MH_DATA_STREAM_BACKEND_H
#define MH_DATA_STREAM_BACKEND_H

#include "interfaces/storage/data_stream.h"

/*
 * Optional operation group on a directory provider; supply all callbacks.
 * Tokens and table are private to that directory's provider. Each callback
 * borrows its context and consumes supplied bytes synchronously. Size/range
 * checks and write/seal/publish ordering are enforced by the dispatcher.
 * Open/create preserve outputs on failure. Publication
 * must finish allocations/checks before changing any visible file or identity.
 * Create/replace use the directory provider's ordinary file tokens. A failed
 * publication retains the sealed token and bytes; close consumes it on all
 * paths.
 */
struct mh_data_stream_ops {
	enum mh_error_code (*open)(
		void	*context, void **stream, struct mh_error *error_out);
	enum mh_error_code (*append)(void *context, void *stream,
		const void	*bytes, size_t size, struct mh_error *error_out);
	enum mh_error_code (*write_at)(void *context, void *stream,
		size_t offset, const void *bytes, size_t size,
		struct mh_error	*error_out);
	enum mh_error_code (*seal)(void *context, void *stream, size_t size,
		struct mh_error	*error_out);
	enum mh_error_code (*create)(void *context, void *stream,
		const char *name, mh_data_publish_check check, void *checker,
		struct mh_data_directory *directory, void **file,
		struct mh_error	*error_out);
	enum mh_error_code (*replace)(void *context, void *stream, void *file,
		mh_data_publish_check check, void *checker,
		struct mh_data_directory *directory,
		struct mh_error	*error_out);
	void			(*close)(void *context, void *stream);
};

#endif
