#define _XOPEN_SOURCE 700
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/filesystem_posix_data_directory_internal.h"
#include "backends/storage/filesystem_posix_sync.h"
#include "backends/storage/filesystem_provider.h"
#include "engine/storage/path_names.h"
#include "interfaces/storage/data_directory_backend.h"

#define TEMP_ATTEMPTS 100

static int
same_file_revision(const struct stat *first, const struct stat *second)
{
	return S_ISREG(first->st_mode) && S_ISREG(second->st_mode) &&
		first->st_dev == second->st_dev &&
		first->st_ino == second->st_ino &&
		first->st_size == second->st_size &&
		first->st_mtim.tv_sec == second->st_mtim.tv_sec &&
		first->st_mtim.tv_nsec == second->st_mtim.tv_nsec;
}

/* Canonical paths keep a symlink into game assets outside this exception. */
static int
protected_asset_path(const char *path, const char *root)
{
	const char *relative;
	size_t length;

	if (root == NULL)
		return 0;

	length = strlen(root);

	if (strncmp(path, root, length) != 0 ||
		(length != 1 && path[length] != 0 && path[length] != '/'))
		return 0;

	relative = path + length;

	if (*relative == '/')
		relative++;

	return strncmp(relative, MH_USER_DATA_DIR,
		sizeof(MH_USER_DATA_DIR) - 1) != 0 ||
		(relative[sizeof(MH_USER_DATA_DIR) - 1] != 0 &&
			relative[sizeof(MH_USER_DATA_DIR) - 1] != '/');
}

static enum mh_error_code
validate_directory(void *context, struct mh_error *error_out)
{
	const struct native_directory *directory;
	struct stat held, named;
	char *path;
	enum mh_error_code status;

	directory = context;

	if (directory == NULL || directory->fd < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing writable directory");

	if (directory->read_only)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"directory is read-only");

	path = realpath(directory->path, NULL);

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"writable directory is unavailable");

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (protected_asset_path(path, directory->protected_root))
		status = mh_error_set(error_out, MH_ERROR_IO,
			"cannot write user data in the original asset tree");
	else if (stat(path, &named) != 0 || fstat(directory->fd, &held) != 0 ||
		named.st_dev != held.st_dev || named.st_ino != held.st_ino)
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"writable directory changed");

	free(path);

	return status;
}

static enum mh_error_code
validate_readable_directory(void *context, struct mh_error *error_out)
{
	struct native_directory *directory;
	struct stat held;

	directory = context;

	if (!directory->browsing)
		return validate_directory(context, error_out);

	if (fstat(directory->fd, &held) != 0 || !S_ISDIR(held.st_mode))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"held directory is unavailable");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_filesystem_file_close(void *context, void *token)
{
	struct native_file *file;

	(void)context;
	file = token;

	if (file == NULL)
		return;

	free(file->name);
	free(file);
}

struct native_file *
mh_filesystem_file_new(const char *name)
{
	struct native_file *file;

	file = calloc(1, sizeof(*file));

	if (file == NULL)
		return NULL;

	file->name = malloc(strlen(name) + 1);

	if (file->name == NULL) {
		free(file);
		return NULL;
	}

	strcpy(file->name, name);

	return file;
}

static void
close_directory(void *context)
{
	struct native_directory *directory;

	directory = context;

	if (directory->fd >= 0)
		close(directory->fd);

	free(directory->path);
	free(directory->protected_root);
	free(directory);
}

static enum mh_error_code
list_directory(void *context, mh_directory_visit visit, void *visitor,
	struct mh_error *error_out)
{
	struct native_directory *directory;
	DIR *stream;
	struct dirent *entry;
	enum mh_directory_action action;
	enum mh_error_code status;
	int fd, system_error;

	directory = context;
	status = validate_readable_directory(directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fd = openat(directory->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if (fd < 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot scan writable directory");

	stream = fdopendir(fd);

	if (stream == NULL) {
		close(fd);
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot scan writable directory");
	}

	action = MH_DIRECTORY_CONTINUE;
	system_error = 0;

	while (action == MH_DIRECTORY_CONTINUE) {
		errno = 0;
		entry = readdir(stream);

		if (entry == NULL) {
			system_error = errno;
			break;
		}

		if (strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0)
			continue;

		action = visit(visitor, entry->d_name);
	}

	if (closedir(stream) != 0 && system_error == 0)
		system_error = errno;

	if (system_error != 0)
		return mh_error_system(error_out, system_error,
			"cannot enumerate writable directory");

	if (action != MH_DIRECTORY_CONTINUE && action != MH_DIRECTORY_STOP)
		return mh_error_set(error_out, MH_ERROR_IO,
			"writable directory callback failed");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_file(void *context, const char *name, void **output,
	struct mh_error *error_out)
{
	struct native_directory *directory;
	struct native_file *file;
	enum mh_error_code status;

	directory = context;
	status = validate_readable_directory(directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	file = mh_filesystem_file_new(name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening stored file");

	if (fstatat(directory->fd, name, &file->identity,
		AT_SYMLINK_NOFOLLOW) != 0)
		status = mh_error_system(
			error_out, errno, "cannot inspect stored file");
	else if (!S_ISREG(file->identity.st_mode))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"stored file is not regular");

	if (status != MH_ERROR_NONE) {
		mh_filesystem_file_close(NULL, file);
		return status;
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_bytes(struct native_directory *directory, int fd, struct native_file *file,
	size_t limit, struct mh_file *output, struct mh_error *error_out)
{
	struct stat after, named;
	struct mh_file bytes;
	unsigned char extra;
	size_t position;
	ssize_t received;
	enum mh_error_code status;

	if (fstat(fd, &file->identity) != 0 ||
		!S_ISREG(file->identity.st_mode) ||
		file->identity.st_size < 0 ||
		(long double)file->identity.st_size > (long double)limit)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"stored file is not regular or exceeds read limit");

	bytes.size = (size_t)file->identity.st_size;
	bytes.data = malloc(bytes.size + 1);

	if (bytes.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading stored file");

	position = 0;
	status = MH_ERROR_NONE;

	while (position < bytes.size) {
		received =
			read(fd, bytes.data + position, bytes.size - position);

		if (received < 0 && errno == EINTR)
			continue;

		if (received <= 0) {
			status = mh_error_set(error_out, MH_ERROR_IO,
				"cannot read complete stored file");
			break;
		}

		position += (size_t)received;
	}

	do {
		received = read(fd, &extra, 1);
	} while (received < 0 && errno == EINTR);

	if (status == MH_ERROR_NONE &&
		(received != 0 || fstat(fd, &after) != 0 ||
			!same_file_revision(&file->identity, &after) ||
			fstatat(directory->fd, file->name, &named,
				AT_SYMLINK_NOFOLLOW) != 0 ||
			!same_file_revision(&after, &named)))
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"stored file changed while reading");

	if (status != MH_ERROR_NONE) {
		free(bytes.data);
		return status;
	}

	bytes.data[bytes.size] = 0;
	*output = bytes;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_file(void *context, const char *name, size_t limit, struct mh_file *bytes,
	void **output, struct mh_error *error_out)
{
	struct native_directory *directory;
	struct native_file *file;
	struct mh_file next;
	enum mh_error_code status;
	int fd, system_error;

	directory = context;
	status = validate_readable_directory(directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	file = mh_filesystem_file_new(name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening stored file");

	fd = openat(directory->fd, name,
		O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0) {
		system_error = errno;
		mh_filesystem_file_close(NULL, file);
		return mh_error_system(
			error_out, system_error, "cannot open stored file");
	}

	next.data = NULL;
	status = read_bytes(directory, fd, file, limit, &next, error_out);

	if (close(fd) != 0 && status == MH_ERROR_NONE)
		status = mh_error_set(
			error_out, MH_ERROR_IO, "cannot close stored file");

	if (status != MH_ERROR_NONE) {
		free(next.data);
		mh_filesystem_file_close(NULL, file);
		return status;
	}

	*bytes = next;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Consumes the temporary descriptor, including fdopen/write failure paths. */
static enum mh_error_code
write_temporary(int fd, const void *bytes, size_t size, struct stat *identity,
	struct mh_error *error_out)
{
	FILE *stream;
	struct stat finished;
	enum mh_error_code status;

	stream = fdopen(fd, "wb");

	if (stream == NULL) {
		close(fd);
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot open stored file stream");
	}

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (fwrite(bytes, 1, size, stream) != size || fflush(stream) != 0 ||
		fsync(fileno(stream)) != 0)
		status = mh_error_set(
			error_out, MH_ERROR_IO, "cannot write stored file");

	if (status == MH_ERROR_NONE) {
		if (fstat(fileno(stream), &finished) != 0)
			status = mh_error_set(error_out, MH_ERROR_IO,
				"cannot inspect stored file temporary file");
		else
			*identity = finished;
	}

	if (fclose(stream) != 0 && status == MH_ERROR_NONE)
		status = mh_error_set(error_out, MH_ERROR_IO,
			"cannot close stored file stream");

	return status;
}

static enum mh_error_code
validate_file(void *context, void *token, struct mh_error *error_out)
{
	struct native_directory *directory;
	struct native_file *file;
	struct stat current;
	enum mh_error_code status;

	directory = context;
	file = token;
	status = validate_readable_directory(directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (fstatat(directory->fd, file->name, &current, AT_SYMLINK_NOFOLLOW) !=
			0 ||
		!same_file_revision(&current, &file->identity))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"stored file changed since loading");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_temporary_open(struct native_directory *directory,
	struct native_temporary *temporary, int *output,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	int fd, attempt;

	status = validate_directory(directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fd = -1;

	for (attempt = 0; attempt < TEMP_ATTEMPTS; attempt++) {
		sprintf(temporary->name, ".mh-data-%lu.tmp.%lu",
			(unsigned long)getpid(), directory->serial++);
		fd = openat(directory->fd, temporary->name,
			O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
			0600);

		if (fd >= 0 || errno != EEXIST)
			break;
	}

	if (fd < 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot create private stored file");

	if (fstat(fd, &temporary->identity) != 0) {
		close(fd);
		unlinkat(directory->fd, temporary->name, 0);
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot inspect private stored file");
	}

	*output = fd;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_filesystem_temporary_remove(struct native_directory *directory,
	const struct native_temporary *temporary)
{
	struct stat named;

	if (fstatat(directory->fd, temporary->name, &named,
		AT_SYMLINK_NOFOLLOW) == 0 &&
		named.st_dev == temporary->identity.st_dev &&
		named.st_ino == temporary->identity.st_ino)
		unlinkat(directory->fd, temporary->name, 0);
}

/* Publish prepared bytes last; only then advance the retained file identity. */
enum mh_error_code
mh_filesystem_temporary_publish(struct native_directory *directory,
	struct native_temporary *temporary, struct native_file *file,
	int replace, mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, struct mh_error *error_out)
{
	struct mh_directory_sync *pending;
	struct stat named;
	enum mh_error_code status;
	int result, saved;

	status = validate_directory(directory, error_out);

	if (status == MH_ERROR_NONE && replace)
		status = validate_file(directory, file, error_out);

	if (status == MH_ERROR_NONE && check != NULL)
		status = check(checker, owner, error_out);

	if (status == MH_ERROR_NONE && replace)
		status = validate_file(directory, file, error_out);

	if (status == MH_ERROR_NONE &&
		(fstatat(directory->fd, temporary->name, &named,
			AT_SYMLINK_NOFOLLOW) != 0 ||
			!same_file_revision(&temporary->identity, &named)))
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"private stored file changed");

	if (status != MH_ERROR_NONE)
		return status;

	if (mh_directory_sync_hold(directory->fd, &pending) != 0)
		return mh_error_system(
			error_out, errno, "cannot retain storage directory");

	if (replace)
		result = renameat(directory->fd, temporary->name, directory->fd,
			file->name);
	else
		result = mh_filesystem_publish_new(
			directory->fd, temporary->name, file->name);

	saved = errno;
	mh_directory_sync_finish(pending, result == 0);

	if (result != 0)
		return mh_error_system(
			error_out, saved, "cannot publish stored file");

	file->identity = temporary->identity;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
publish_file(struct native_directory *directory, struct native_file *file,
	const void *bytes, size_t size, int replace,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, struct mh_error *error_out)
{
	struct native_temporary temporary;
	enum mh_error_code status;
	int fd;

	status = validate_directory(directory, error_out);

	if (status == MH_ERROR_NONE && replace)
		status = validate_file(directory, file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_filesystem_temporary_open(
		directory, &temporary, &fd, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = write_temporary(
		fd, bytes, size, &temporary.identity, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_filesystem_temporary_publish(directory, &temporary,
			file, replace, check, checker, owner, error_out);

	mh_filesystem_temporary_remove(directory, &temporary);

	return status;
}

static enum mh_error_code
create_file(void *context, const char *name, const void *bytes, size_t size,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, void **output,
	struct mh_error *error_out)
{
	struct native_file *file;
	enum mh_error_code status;

	status = validate_directory(context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	file = mh_filesystem_file_new(name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating stored file");

	status = publish_file(context, file, bytes, size, 0, check, checker,
		owner, error_out);

	if (status != MH_ERROR_NONE) {
		mh_filesystem_file_close(NULL, file);
		return status;
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
replace_file(void *context, void *token, const void *bytes, size_t size,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *owner, struct mh_error *error_out)
{
	return publish_file(context, token, bytes, size, 1, check, checker,
		owner, error_out);
}

static enum mh_error_code
unlink_file(struct native_directory *directory, const char *name,
	struct mh_error *error_out)
{
	struct mh_directory_sync *pending;
	int result, saved;

	if (mh_directory_sync_hold(directory->fd, &pending) != 0)
		return mh_error_system(
			error_out, errno, "cannot retain storage directory");

	result = unlinkat(directory->fd, name, 0);
	saved = errno;
	mh_directory_sync_finish(pending, result == 0);

	if (result != 0)
		return mh_error_system(
			error_out, saved, "cannot delete stored file");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
remove_file(void *context, void *token, struct mh_error *error_out)
{
	struct native_directory *directory;
	struct native_file *file;
	enum mh_error_code status;

	directory = context;
	file = token;
	status = validate_directory(directory, error_out);

	if (status == MH_ERROR_NONE)
		status = validate_file(directory, file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return unlink_file(directory, file->name, error_out);
}

static enum mh_error_code
protect_directory(
	void *context, const char *resource_root, struct mh_error *error_out)
{
	struct native_directory *directory;
	char *previous, *root;
	enum mh_error_code status;

	directory = context;
	root = realpath(resource_root, NULL);

	if (root == NULL)
		return mh_error_system(
			error_out, errno, "cannot resolve protected directory");

	previous = directory->protected_root;
	directory->protected_root = root;
	status = validate_directory(directory, error_out);

	if (status != MH_ERROR_NONE) {
		directory->protected_root = previous;
		free(root);
		return status;
	}

	free(previous);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
refresh_file(void *context, void *token, struct mh_error *error_out)
{
	struct native_directory *directory;
	struct native_file *file;
	struct stat current;
	enum mh_error_code status;

	directory = context;
	file = token;
	status = validate_directory(directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (fstatat(directory->fd, file->name, &current, AT_SYMLINK_NOFOLLOW) !=
			0 ||
		!S_ISREG(current.st_mode) ||
		current.st_dev != file->identity.st_dev ||
		current.st_ino != file->identity.st_ino)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "stored file entry changed");

	file->identity = current;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const char *
directory_path(void *context)
{
	struct native_directory *directory;

	directory = context;

	return directory->path;
}

static enum mh_error_code
same_directory(
	void *context, void *other, int *same, struct mh_error *error_out)
{
	struct native_directory *directory, *other_directory;
	struct stat first, second;

	directory = context;
	other_directory = other;

	if (fstat(directory->fd, &first) != 0 ||
		fstat(other_directory->fd, &second) != 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot compare held directories");

	*same = first.st_dev == second.st_dev && first.st_ino == second.st_ino;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
inspect_entry(void *context, const char *name, void **output, int *removable,
	struct mh_error *error_out)
{
	struct native_directory *directory;
	struct native_file *file;
	struct stat info;

	directory = context;

	if (fstatat(directory->fd, name, &info, AT_SYMLINK_NOFOLLOW) != 0)
		return mh_error_system(
			error_out, errno, "cannot inspect directory entry");

	file = NULL;

	if (S_ISREG(info.st_mode)) {
		file = mh_filesystem_file_new(name);

		if (file == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory inspecting directory entry");

		file->identity = info;
	}

	*output = file;
	*removable = file != NULL && !directory->read_only;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
remove_entry(void *context, void *token, struct mh_error *error_out)
{
	struct native_directory *directory;
	struct native_file *file;
	struct stat info;

	directory = context;
	file = token;

	if (directory->read_only)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"directory is read-only");

	if (fstatat(directory->fd, file->name, &info, AT_SYMLINK_NOFOLLOW) !=
			0 ||
		!S_ISREG(info.st_mode) ||
		info.st_dev != file->identity.st_dev ||
		info.st_ino != file->identity.st_ino)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"directory entry changed since selection");

	return unlink_file(directory, file->name, error_out);
}

static enum mh_error_code
open_directory(const char *path, const char *protected_root, int browsing,
	int read_only, struct mh_data_directory **output,
	struct mh_error *error_out)
{
	static const struct mh_data_browse_ops browse = {
		directory_path, same_directory, inspect_entry, remove_entry};

	static const struct mh_data_directory_ops ops = {
		validate_readable_directory, list_directory, read_file,
		create_file, remove_file, mh_filesystem_file_close,
		close_directory, validate_file, replace_file, open_file,
		&mh_filesystem_stream_ops, protect_directory, refresh_file,
		&browse};

	struct native_directory *directory;
	enum mh_error_code status;

	if (path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid writable directory paths");

	directory = calloc(1, sizeof(*directory));

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening writable directory");

	directory->fd = -1;
	directory->browsing = browsing;
	directory->read_only = read_only;

	if (protected_root != NULL)
		directory->protected_root = realpath(protected_root, NULL);

	status = MH_ERROR_NONE;

	if (protected_root != NULL && directory->protected_root == NULL)
		status = mh_error_system(
			error_out, errno, "cannot resolve protected directory");

	if (status == MH_ERROR_NONE) {
		directory->path = realpath(path[0] == 0 ? "." : path, NULL);

		if (directory->path == NULL)
			status = mh_error_system(error_out, errno,
				"cannot resolve writable directory");
	}

	if (status == MH_ERROR_NONE && browsing &&
		protected_asset_path(
			directory->path, directory->protected_root))
		directory->read_only = 1;

	if (status == MH_ERROR_NONE) {
		directory->fd = open(
			directory->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

		if (directory->fd < 0)
			status = mh_error_system(error_out, errno,
				"cannot open writable directory");
		else
			status = validate_readable_directory(
				directory, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_data_directory_bind(
			&ops, directory, output, error_out);

	if (status != MH_ERROR_NONE)
		close_directory(directory);

	return status;
}

enum mh_error_code
mh_filesystem_data_directory_open(const char *path, const char *protected_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	return open_directory(path, protected_root, 0, 0, output, error_out);
}

enum mh_error_code
mh_filesystem_data_directory_browse(const char *path,
	const char *protected_root, int read_only,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	if (protected_root == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing catalog resource root");

	return open_directory(
		path, protected_root, 1, read_only != 0, output, error_out);
}
