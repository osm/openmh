#ifndef MH_MEMORY_USER_DATA_INTERNAL_H
#define MH_MEMORY_USER_DATA_INTERNAL_H

#include "backends/storage/memory_user_data.h"
#include "interfaces/storage/data_directory_backend.h"

/* Owns key/bytes; generation identifies the last published file revision. */
struct memory_entry {
	char			*key;
	unsigned char		*bytes;
	size_t			size;
	int			directory;
	unsigned long		generation;
	struct memory_entry	*next;
};

/* Owns the sorted entry list; held directories borrow this view. */
struct memory_data {
	struct mh_user_data	data;
	struct memory_entry	*entries;
	unsigned long		generation;
};

struct memory_entry	*mh_memory_data_find(const struct memory_data *memory,
	const char *key);
void			 mh_memory_data_insert(struct memory_data *memory,
	struct memory_entry *entry);
int			 mh_memory_data_conflicts(
	const struct memory_data *memory, const char *key, int directory);
struct memory_data	*mh_memory_data_context(
	const struct mh_user_data *data);

/* Retains a borrowed memory user-data view through directory close. */
enum mh_error_code	mh_memory_data_directory_open(
	const struct mh_user_data *data, const char *path,
	struct mh_data_directory **output, struct mh_error *error_out);

/*
 * Non-creating memory catalog view; unlike writable open it does not pin paths.
 */
enum mh_error_code	mh_memory_data_directory_browse(
	const struct mh_user_data *data, const char *path,
	struct mh_data_directory **output, struct mh_error *error_out);

#endif
