#define _XOPEN_SOURCE 700
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/filesystem_tree.h"

enum {
	COPY_DEPTH_LIMIT = 64,
	COPY_BUFFER_SIZE = 65536
};

struct tree_copy {
	mh_filesystem_copy_progress	progress;
	void				*context;
	unsigned char			buffer[COPY_BUFFER_SIZE];
};

static enum mh_error_code
copy_bytes(struct tree_copy *copy, int input, int output,
	struct mh_error *error_out)
{
	ssize_t count, written;
	size_t offset;

	for (;;) {
		count = read(input, copy->buffer, sizeof(copy->buffer));

		if (count < 0 && errno == EINTR)
			continue;

		if (count < 0)
			return mh_error_system(
				error_out, errno, "cannot read source file");

		if (count == 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		offset = 0;

		while (offset < (size_t)count) {
			written = write(output, copy->buffer + offset,
				(size_t)count - offset);

			if (written < 0 && errno == EINTR)
				continue;

			if (written <= 0)
				return mh_error_system(
					error_out, errno, "cannot copy file");

			offset += (size_t)written;
		}
	}
}

static enum mh_error_code
copy_file(struct tree_copy *copy, int source, int destination, const char *name,
	struct mh_error *error_out)
{
	struct stat before, after;
	int input, output;
	enum mh_error_code status;

	input = openat(
		source, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);

	if (input < 0)
		return mh_error_system(
			error_out, errno, "cannot open source file");

	if (fstat(input, &before) != 0 || !S_ISREG(before.st_mode)) {
		close(input);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"copy source is not a regular file");
	}

	output = openat(destination, name,
		O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

	if (output < 0) {
		status = mh_error_system(
			error_out, errno, "cannot create staged file");
		close(input);
		return status;
	}

	status = copy_bytes(copy, input, output, error_out);

	if (status == MH_ERROR_NONE &&
		(fstat(input, &after) != 0 || before.st_size != after.st_size ||
			before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
			before.st_mtim.tv_nsec != after.st_mtim.tv_nsec))
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"source file changed while copying; close the game before patching");

	if (status == MH_ERROR_NONE &&
		fchmod(output, before.st_mode & 0777) != 0)
		status = mh_error_system(
			error_out, errno, "cannot preserve file mode");

	if (close(output) != 0 && status == MH_ERROR_NONE)
		status = mh_error_system(
			error_out, errno, "cannot close staged file");

	close(input);

	return status;
}

static enum mh_error_code	copy_directory(struct tree_copy *, int, int,
	unsigned int, struct mh_error *);

static enum mh_error_code
copy_entry(struct tree_copy *copy, int source, int destination,
	const char *name, unsigned int depth, struct mh_error *error_out)
{
	struct stat info;
	int input, output;
	enum mh_error_code status;

	if (copy->progress != NULL) {
		status = copy->progress(copy->context, name, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (fstatat(source, name, &info, AT_SYMLINK_NOFOLLOW) != 0)
		return mh_error_system(
			error_out, errno, "cannot inspect source entry");

	if (S_ISREG(info.st_mode))
		return copy_file(copy, source, destination, name, error_out);

	if (!S_ISDIR(info.st_mode))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"extraction copies regular files and directories; links/devices are unsupported");

	if (mkdirat(destination, name, 0700) != 0)
		return mh_error_system(
			error_out, errno, "cannot create staged directory");

	input = openat(
		source, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	output = openat(destination, name,
		O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

	if (input < 0 || output < 0)
		status = mh_error_system(
			error_out, errno, "cannot open copied directory");
	else
		status = copy_directory(
			copy, input, output, depth + 1, error_out);

	if (input >= 0)
		close(input);

	if (output >= 0)
		close(output);

	return status;
}

static enum mh_error_code
copy_directory(struct tree_copy *copy, int source, int destination,
	unsigned int depth, struct mh_error *error_out)
{
	DIR *directory;
	struct dirent *entry;
	int scan;
	enum mh_error_code status;

	if (depth > COPY_DEPTH_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"directory tree is too deep");

	scan = dup(source);

	if (scan < 0)
		return mh_error_system(
			error_out, errno, "cannot duplicate source directory");

	directory = fdopendir(scan);

	if (directory == NULL) {
		close(scan);
		return mh_error_system(
			error_out, errno, "cannot scan source directory");
	}

	status = MH_ERROR_NONE;

	while (status == MH_ERROR_NONE) {
		errno = 0;
		entry = readdir(directory);

		if (entry == NULL) {
			if (errno != 0)
				status = mh_error_system(error_out, errno,
					"cannot scan source directory");

			break;
		}

		if (strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0)
			continue;

		status = copy_entry(copy, source, destination, entry->d_name,
			depth, error_out);
	}

	closedir(directory);

	return status;
}

enum mh_error_code
mh_filesystem_copy_tree(const char *source, const char *destination,
	mh_filesystem_copy_progress progress, void *context,
	struct mh_error *error_out)
{
	struct tree_copy *copy;
	char *from, *to;
	size_t length;
	int input, output;
	enum mh_error_code status;

	from = realpath(source, NULL);
	to = realpath(destination, NULL);

	if (from == NULL || to == NULL) {
		free(from);
		free(to);
		return mh_error_set(
			error_out, MH_ERROR_IO, "copy directories must exist");
	}

	length = strlen(from);

	if (strncmp(from, to, length) == 0 &&
		(length == 1 || to[length] == 0 || to[length] == '/')) {
		free(from);
		free(to);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"output must be outside the source directory");
	}

	free(from);
	free(to);
	copy = malloc(sizeof(*copy));

	if (copy == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying tree");

	copy->progress = progress;
	copy->context = context;
	input = open(source, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	output = open(
		destination, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

	if (input < 0 || output < 0)
		status = mh_error_system(
			error_out, errno, "cannot open copy directories");
	else
		status = copy_directory(copy, input, output, 0, error_out);

	if (input >= 0)
		close(input);

	if (output >= 0)
		close(output);

	free(copy);

	return status;
}
