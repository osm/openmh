#ifndef MH_DATA_DIRECTORY_BACKEND_H
#define MH_DATA_DIRECTORY_BACKEND_H

#include "interfaces/storage/data_directory.h"

struct mh_data_stream_ops;

/*
 * Optional catalog inspection on an already held directory. path returns a
 * borrowed canonical/native or logical locator. same compares contexts from
 * this same operation group; unrelated implementations are always distinct.
 * inspect reports a regular-file token, or NULL for a visible nonregular
 * entry. removable describes provider policy, not OS access permissions.
 * remove_entry checks entry identity rather than the content revision and
 * operates on the held directory even if its original pathname has moved.
 * All returned tokens use the directory's ordinary file_close callback.
 * remove_entry may be NULL on read-only views.
 */
struct mh_data_browse_ops {
	const char		*(*path)(void *context);
	enum mh_error_code (*same)(void *context, void *other, int *same,
		struct mh_error	*error_out);
	enum mh_error_code (*inspect)(void *context, const char *name,
		void	**file, int *removable, struct mh_error *error_out);
	enum mh_error_code (*remove_entry)(
		void	*context, void *file, struct mh_error *error_out);
};

/*
 * Mutation callbacks create/remove/replace may be NULL on read-only views.
 * File tokens belong to the provider and are released with file_close.
 * read/open_file/create leave outputs unchanged on failure. create invokes
 * check after preparing bytes and before publication; it must not fail after
 * publication. Callbacks consume input bytes synchronously. close owns
 * provider-context cleanup. The operation table stays borrowed and unchanged
 * until the directory is closed.
 */
struct mh_data_directory_ops {
	enum mh_error_code (*validate)(
		void			*context, struct mh_error *error_out);
	enum mh_error_code (*list)(void *context, mh_directory_visit visit,
		void			*visitor, struct mh_error *error_out);
	enum mh_error_code (*read)(void *context, const char *name,
		size_t limit, struct mh_file *bytes, void **file,
		struct mh_error		*error_out);
	enum mh_error_code (*create)(void *context, const char *name,
		const void *bytes, size_t size, mh_data_publish_check check,
		void *checker, struct mh_data_directory *directory, void **file,
		struct mh_error		*error_out);
	enum mh_error_code (*remove)(
		void	*context, void *file, struct mh_error *error_out);
	void	(*file_close)(void *context, void *file);
	void				(*close)(void *context);
	enum mh_error_code (*file_validate)(
		void	*context, void *file, struct mh_error *error_out);

	/* Update the token only after successful publication. */
	enum mh_error_code (*replace)(void *context, void *file,
		const void *bytes, size_t size, mh_data_publish_check check,
		void *checker, struct mh_data_directory *directory,
		struct mh_error		*error_out);

	/*
	 * Optional identity-only capture; same token lifetime as read/create.
	 */
	enum mh_error_code (*open_file)(void *context, const char *name,
		void			**file, struct mh_error *error_out);

	/* Optional private streaming output. */
	const struct mh_data_stream_ops	*streams;

	/* Optional protection update and same-entry revision refresh. */
	enum mh_error_code (*protect)(void *context, const char *resource_root,
		struct mh_error		*error_out);
	enum mh_error_code (*file_refresh)(
		void	*context, void *file, struct mh_error *error_out);
	const struct mh_data_browse_ops	*browse;
};

/* Takes context ownership only on success. */
enum mh_error_code	mh_data_directory_bind(
	const struct mh_data_directory_ops *ops, void *context,
	struct mh_data_directory **output, struct mh_error *error_out);
#endif
