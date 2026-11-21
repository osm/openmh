#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/archive/unshield_internal.h"
#include "backends/archive/unshield_zip.h"

struct memory_file {
	struct cabinet	*cabinet;
	size_t		position;
	int		writing;
};

static void *
open_file(const char *name, const char *mode, void *context)
{
	struct memory_file *file;
	struct cabinet *cabinet;
	int writing;

	cabinet = context;
	writing = strcmp(mode, "wb") == 0;

	if (writing) {
		if (strcmp(name, "member") != 0 || cabinet->output == NULL)
			return NULL;
	} else if (strcmp(mode, "rb") != 0 ||
		strcmp(name, MH_UNSHIELD_CABINET_NAME) != 0)
		return NULL;

	file = malloc(sizeof(*file));

	if (file == NULL)
		return NULL;

	file->cabinet = cabinet;
	file->position = 0;
	file->writing = writing;

	return file;
}

static int
seek_file(void *handle, long offset, int whence, void *context)
{
	struct memory_file *file;
	size_t size;
	long base;

	(void)context;
	file = handle;
	size = file->writing ? file->cabinet->output_size : file->cabinet->size;

	if (size > LONG_MAX)
		return -1;

	if (whence == SEEK_SET)
		base = 0;
	else if (whence == SEEK_CUR)
		base = (long)file->position;
	else if (whence == SEEK_END)
		base = (long)size;
	else
		return -1;

	if (offset < -base || offset > (long)size - base)
		return -1;

	file->position = (size_t)(base + offset);

	return 0;
}

static long
tell_file(void *handle, void *context)
{
	struct memory_file *file;

	(void)context;
	file = handle;

	return (long)file->position;
}

static size_t
read_file(void *output, size_t size, size_t count, void *handle, void *context)
{
	struct memory_file *file;
	size_t available;

	(void)context;
	file = handle;

	if (size == 0 || file->writing)
		return 0;

	available = (file->cabinet->size - file->position) / size;

	if (count > available)
		count = available;

	memcpy(output, file->cabinet->bytes + file->position, size * count);
	file->position += size * count;

	return count;
}

static size_t
write_file(const void *bytes, size_t size, size_t count, void *handle,
	void *context)
{
	struct memory_file *file;
	struct cabinet *cabinet;

	(void)context;
	file = handle;
	cabinet = file->cabinet;

	if (size == 0 || !file->writing ||
		count > (cabinet->output_size - file->position) / size)
		return 0;

	memcpy(cabinet->output + file->position, bytes, size * count);
	file->position += size * count;
	cabinet->written = file->position;

	return count;
}

static int
close_file(void *handle, void *context)
{
	(void)context;
	free(handle);

	return 0;
}

static void *
open_directory(const char *name, void *context)
{
	(void)name;
	(void)context;

	return NULL;
}

static int
close_directory(void *handle, void *context)
{
	(void)handle;
	(void)context;

	return 0;
}

static struct dirent *
read_directory(void *handle, void *context)
{
	(void)handle;
	(void)context;

	return NULL;
}

const UnshieldIoCallbacks mh_unshield_memory_io = {open_file, seek_file,
	tell_file, read_file, write_file, close_file, open_directory,
	close_directory, read_directory};
