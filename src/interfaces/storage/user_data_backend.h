#ifndef MH_USER_DATA_BACKEND_H
#define MH_USER_DATA_BACKEND_H

#include "interfaces/storage/user_data.h"

/*
 * Binding and context remain borrowed and unchanged through the last user.
 * Callbacks access an already prepared local view; they do not fetch remote
 * data or wait for persistence. The host composes this view with the existing
 * storage PREPARE/COMMIT service and retains uncommitted local mutations across
 * commit failures. No operation may write to packaged resources implicitly.
 */
struct mh_user_data_ops {
	enum mh_error_code (*read)(void *context, const char *key, size_t limit,
		struct mh_file	*output, struct mh_error *error_out);
	enum mh_error_code (*replace)(void *context, const char *key,
		const void	*bytes, size_t size, struct mh_error *error_out);

	/* Optional default root, resolved only after PREPARE. */
	enum mh_error_code (*directory)(
		void	*context, char **output, struct mh_error *error_out);
	enum mh_error_code (*list)(void *context, const char *path,
		mh_directory_visit visit, void *visitor,
		struct mh_error	*error_out);
	enum mh_error_code (*create_directory)(
		void	*context, const char *path, struct mh_error *error_out);
	enum mh_error_code (*open_directory)(void *context, const char *path,
		const char *resource_root, struct mh_data_directory **output,
		struct mh_error	*error_out);

	/*
	 * Optional path check, without enumerating children or creating
	 * anything.
	 */
	enum mh_error_code (*check_directory)(
		void	*context, const char *path, struct mh_error *error_out);

	/*
	 * Optional non-creating catalog view, including protected read-only
	 * paths.
	 */
	enum mh_error_code (*browse_directory)(void *context, const char *path,
		const char *resource_root, struct mh_data_directory **output,
		struct mh_error	*error_out);
};

struct mh_user_data {
	const struct mh_user_data_ops	*ops;
	void				*context;
};

/* Invalid bindings leave data unchanged; initialization performs no I/O. */
enum mh_error_code	mh_user_data_init(struct mh_user_data *data,
	const struct mh_user_data_ops *ops, void *context,
	struct mh_error *error_out);

#endif
