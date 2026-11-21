#ifndef MH_ZIP_RESOURCE_INTERNAL_H
#define MH_ZIP_RESOURCE_INTERNAL_H

#include "interfaces/storage/resource_backend.h"

#define MH_ZIP_PATH_MAX 1024
#define MH_ZIP_ENTRY_MAX 65534U

struct mh_zip_entry {
	char		*path;
	unsigned long	offset, size, packed, crc, local;
	unsigned int	method;
	int		directory;
};

struct mh_zip_resources {
	struct mh_resource_source	view;
	const struct mh_resource_source	*source;
	char				*path;
	struct mh_zip_entry		*entries;
	size_t				count;
	unsigned long			length, central;

	/* At most one expanded entry, bounded by MH_RESOURCE_READ_LIMIT. */
	const struct mh_zip_entry	*cached_entry;
	struct mh_file			cached_file;
};
enum mh_error_code	mh_zip_inflate(const struct mh_zip_resources *zip,
	const struct mh_zip_entry *entry, struct mh_file *output,
	struct mh_error *error_out);
enum mh_error_code	mh_zip_index(struct mh_zip_resources *zip,
	struct mh_error *error_out);
void			mh_zip_index_free(struct mh_zip_resources *zip);
int			mh_zip_compare_entries(const void *left,
	const void *right);

#endif
