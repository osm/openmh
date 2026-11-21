#ifdef MH_HAVE_USER_DATA_FILES
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_user_data.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "user directories:%d: %s\n", __LINE__, \
				#x);                                           \
			return 0;                                              \
		}                                                              \
	} while (0)

struct directory_listing {
	const char			*const *names;
	unsigned int			count, seen_mask, calls;
	enum mh_directory_action	action;
};

static enum mh_directory_action
inspect_name(void *context, const char *name)
{
	struct directory_listing *listing;
	unsigned int i;

	listing = context;
	listing->calls++;

	if (listing->action != MH_DIRECTORY_CONTINUE)
		return listing->action;

	for (i = 0; i < listing->count; i++) {
		if (strcmp(name, listing->names[i]) != 0)
			continue;

		if (listing->seen_mask & (1U << i))
			return MH_DIRECTORY_FAIL;

		listing->seen_mask |= 1U << i;

		return MH_DIRECTORY_CONTINUE;
	}

	return MH_DIRECTORY_FAIL;
}

static int
check_names(const struct mh_user_data *data, const char *path,
	const char *const *names, unsigned int count)
{
	struct directory_listing listing;

	memset(&listing, 0, sizeof(listing));
	listing.names = names;
	listing.count = count;
	CHECK(mh_user_data_list(data, path, inspect_name, &listing, NULL) ==
		MH_ERROR_NONE);
	CHECK(listing.calls == count && listing.seen_mask == (1U << count) - 1);

	return 1;
}

/* Both providers use these exact namespace operations and expected catalogs. */
static int
check_directories(const struct mh_user_data *data, const char *root)
{
	static const char *const directories[] = {
		"Controls/Empty", "Controls/Sub", "Controls-old"};

	static const char *const files[] = {"Controls/First.CLO",
		"Controls/Sub/Deep.CLO", "Controls/Sub/Other.CLO",
		"Controls/Sub-old", "Controls-old/Side", ".hidden"};

	static const char *const root_names[] = {
		"Controls", "Controls-old", ".hidden"};

	static const char *const control_names[] = {
		"Empty", "First.CLO", "Sub", "Sub-old"};

	static const char *const sub_names[] = {"Deep.CLO", "Other.CLO"};
	struct mh_user_data limited;
	struct mh_data_directory *directory;
	struct mh_user_data_ops ops;
	struct mh_file bytes;
	struct directory_listing listing;
	char prefix[256], path[512];
	unsigned int i;

	CHECK(strlen(root) < sizeof(prefix) - 2);
	strcpy(prefix, root);

	if (root[0] != 0)
		strcat(prefix, "/");

	CHECK(check_names(data, root, NULL, 0));
	CHECK(mh_user_data_check_directory(data, root, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, root, NULL) == MH_ERROR_NONE);

	for (i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
		sprintf(path, "%s%s", prefix, directories[i]);
		CHECK(mh_user_data_create_directory(data, path, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_user_data_create_directory(data, path, NULL) ==
			MH_ERROR_NONE);
	}

	for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		sprintf(path, "%s%s", prefix, files[i]);
		CHECK(mh_user_data_replace(data, path, "saved", 5, NULL) ==
			MH_ERROR_NONE);
	}

	CHECK(check_names(data, root, root_names, 3));
	sprintf(path, "%sControls", prefix);
	CHECK(check_names(data, path, control_names, 4));
	CHECK(mh_user_data_check_directory(data, path, NULL) == MH_ERROR_NONE);
	strcat(path, "/");
	CHECK(check_names(data, path, control_names, 4));
	CHECK(mh_user_data_check_directory(data, path, NULL) == MH_ERROR_NONE);
	sprintf(path, "%sControls/Sub", prefix);
	CHECK(check_names(data, path, sub_names, 2));
	sprintf(path, "%sControls/Empty", prefix);
	CHECK(check_names(data, path, NULL, 0));
	CHECK(mh_user_data_check_directory(data, path, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, path, "x", 1, NULL) != MH_ERROR_NONE);
	CHECK(check_names(data, path, NULL, 0));
	CHECK(mh_user_data_check_directory(data, path, NULL) == MH_ERROR_NONE);

	memset(&listing, 0, sizeof(listing));
	listing.action = MH_DIRECTORY_STOP;
	CHECK(mh_user_data_list(data, root, inspect_name, &listing, NULL) ==
		MH_ERROR_NONE);
	CHECK(listing.calls == 1);
	listing.action = MH_DIRECTORY_FAIL;
	listing.calls = 0;
	CHECK(mh_user_data_list(data, root, inspect_name, &listing, NULL) ==
		MH_ERROR_IO);
	CHECK(listing.calls == 1);
	listing.calls = 0;
	sprintf(path, "%sMissing", prefix);
	CHECK(mh_user_data_check_directory(data, path, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(mh_user_data_list(data, path, inspect_name, &listing, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(listing.calls == 0);
	sprintf(path, "%sControls/First.CLO", prefix);
	CHECK(mh_user_data_check_directory(data, path, NULL) == MH_ERROR_IO);
	CHECK(mh_user_data_list(data, path, inspect_name, &listing, NULL) ==
		MH_ERROR_IO);
	CHECK(listing.calls == 0);
	CHECK(mh_user_data_create_directory(data, path, NULL) != MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, path, 5, &bytes, NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == 5 && memcmp(bytes.data, "saved", 5) == 0);
	free(bytes.data);
	strcat(path, "/Child");
	CHECK(mh_user_data_create_directory(data, path, NULL) != MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, path, "x", 1, NULL) != MH_ERROR_NONE);

	ops = *data->ops;
	ops.list = NULL;
	ops.create_directory = NULL;
	ops.open_directory = NULL;
	ops.check_directory = NULL;
	directory = NULL;
	CHECK(mh_user_data_init(&limited, &ops, data->context, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_list(&limited, root, inspect_name, &listing, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_user_data_create_directory(&limited, root, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_user_data_list(data, root, NULL, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_user_data_create_directory(data, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_user_data_open_directory(&limited, root, "assets", &directory,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(directory == NULL);
	CHECK(mh_user_data_check_directory(&limited, root, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_user_data_check_directory(data, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(check_names(data, root, root_names, 3));

	return 1;
}

#ifndef MH_HAVE_USER_DATA_FILES
static int
check_memory_directories(void)
{
	static const char *const parent_names[] = {"Deep"};
	static const char *const file_names[] = {"Save.CFG"};
	const struct mh_user_data *data, *other;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other, NULL) == MH_ERROR_NONE);
	CHECK(check_directories(data, ""));
	CHECK(check_names(other, "", NULL, 0));

	/* A flat cached key also exposes its implicit parent directories. */
	CHECK(mh_user_data_replace(other, "Parent/Deep/Save.CFG", "", 0,
		NULL) == MH_ERROR_NONE);
	CHECK(check_names(other, "Parent", parent_names, 1));
	CHECK(check_names(other, "Parent/Deep", file_names, 1));
	CHECK(mh_user_data_replace(other, "Parent", "x", 1, NULL) ==
		MH_ERROR_EXISTS);
	CHECK(mh_user_data_create_directory(other, "Parent/Deep", NULL) ==
		MH_ERROR_NONE);
	CHECK(check_names(other, "Parent", parent_names, 1));
	mh_memory_user_data_close(other);
	mh_memory_user_data_close(data);

	return 1;
}
#else

static int
check_native_root(void)
{
	const struct mh_user_data *data;
	char root[] = "/tmp/mh-user-root-XXXXXX", previous[4096];
	char *resolved;

	CHECK(getcwd(previous, sizeof(previous)) != NULL);
	CHECK(mkdtemp(root) != NULL && chdir(root) == 0);
	CHECK(setenv("XDG_DATA_HOME", "/unavailable-user-data", 1) == 0);
	CHECK(unsetenv("HOME") == 0);
	data = mh_filesystem_user_data();
	CHECK(access("userdata", F_OK) != 0);
	CHECK(mh_user_data_directory(data, &resolved, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(resolved, "userdata") == 0);
	free(resolved);
	CHECK(mh_user_data_directory(data, &resolved, NULL) == MH_ERROR_NONE);
	free(resolved);
	CHECK(rmdir("userdata") == 0);

	/* A file at the reserved path fails without modifying it or output. */
	CHECK(mh_user_data_replace(data, "userdata", "saved", 5, NULL) ==
		MH_ERROR_NONE);
	resolved = root;
	CHECK(mh_user_data_directory(data, &resolved, NULL) != MH_ERROR_NONE);
	CHECK(resolved == root);
	CHECK(unlink("userdata") == 0);
	CHECK(chdir(previous) == 0 && rmdir(root) == 0);

	return 1;
}

static int
check_native_directories(void)
{
	static const char *const files[] = {"Controls/First.CLO",
		"Controls/Sub/Deep.CLO", "Controls/Sub/Other.CLO",
		"Controls/Sub-old", "Controls-old/Side", ".hidden"};

	static const char *const directories[] = {
		"Controls/Empty", "Controls/Sub", "Controls", "Controls-old"};

	char root[] = "/tmp/mh-user-directories-XXXXXX", path[512];
	unsigned int i;
	int success;

	CHECK(mkdtemp(root) != NULL);
	success = check_directories(mh_filesystem_user_data(), root);

	for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		sprintf(path, "%s/%s", root, files[i]);
		(void)unlink(path);
	}

	for (i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
		sprintf(path, "%s/%s", root, directories[i]);
		CHECK(rmdir(path) == 0);
	}

	CHECK(rmdir(root) == 0);

	return success;
}
#endif

int
main(void)
{
#ifdef MH_HAVE_USER_DATA_FILES
	if (!check_native_root() || !check_native_directories())
		return 1;

#else
	if (!check_memory_directories())
		return 1;
#endif

	puts("User directories: nested and empty catalogs, unique names, stop/failure, file conflicts and optional providers passed");

	return 0;
}
