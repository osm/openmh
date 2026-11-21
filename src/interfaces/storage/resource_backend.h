#ifndef MH_RESOURCE_BACKEND_H
#define MH_RESOURCE_BACKEND_H

#include "interfaces/storage/resource.h"

/*
 * A source borrows its operations/context; the binding stays unchanged. The
 * host retains both until all users finish, and serializes callbacks unless
 * the provider promises more.
 * Preparation and durable commits use the existing storage lifecycle.
 * These callbacks access a ready local view; they never fetch network data.
 * Reads transfer malloc-owned bytes, including an allocation for empty data.
 * Providers enforce the supplied byte limit before allocating; size queries
 * report the full size independently of that read limit.
 * Read/size failures preserve outputs. List names live only during visit.
 */
struct mh_resource_ops {
	enum mh_error_code (*read)(void *context, const char *path,
		size_t limit, struct mh_file *output,
		struct mh_error			*error_out);
	enum mh_error_code (*size)(void *context, const char *path,
		unsigned long	*output, struct mh_error *error_out);

	/* Optional enumeration of immediate children, unsorted and unique. */
	enum mh_error_code (*list)(void *context, const char *path,
		mh_directory_visit visit, void *visitor,
		struct mh_error			*error_out);

	/*
	 * Optional exact range reads for metadata, without loading whole
	 * resources.
	 */
	enum mh_error_code (*read_at)(void *context, const char *path,
		unsigned long offset, void *output, size_t size,
		struct mh_error			*error_out);

	/*
	 * Optional held, read-only directory; borrows this source through
	 * close.
	 */
	enum mh_error_code (*open_directory)(void *context, const char *path,
		struct mh_data_directory	**output, struct mh_error *error_out);
};

struct mh_resource_source {
	const struct mh_resource_ops	*ops;
	void				*context;
};

/* Invalid operations leave source unchanged; initialization performs no I/O. */
enum mh_error_code	mh_resource_source_init(
	struct mh_resource_source *source, const struct mh_resource_ops *ops,
	void *context, struct mh_error *error_out);

#endif
