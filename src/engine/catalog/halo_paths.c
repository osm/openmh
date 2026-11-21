#include <string.h>

#include "engine/catalog/halo_paths.h"

enum mh_error_code
mh_halo_paths_append(struct mh_halo_paths *paths, const char *argument,
	struct mh_error *error_out)
{
	const char *start;
	size_t length;

	if (paths == NULL || argument == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing halo path inputs");

	while (*argument == ' ' || *argument == '\t')
		argument++;

	start = argument;

	while (*argument != 0 && *argument != ' ' && *argument != '\t')
		argument++;

	length = (size_t)(argument - start);

	while (*argument == ' ' || *argument == '\t')
		argument++;

	if (length == 0 || length >= MH_HALO_PATH_SIZE || *argument != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Halo path");

	if (paths->count >= MH_HALO_TEXTURES ||
		paths->size >= sizeof(paths->names) ||
		length + 1 >= sizeof(paths->names) - paths->size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"Halo path list is too long");

	memcpy(paths->names + paths->size, start, length);
	paths->names[paths->size + length] = 0;
	paths->size += length + 1;
	paths->names[paths->size] = 0;
	paths->count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const char *
mh_halo_path(const struct mh_halo_paths *paths, unsigned int index)
{
	const char *end;
	size_t offset;
	unsigned int i;

	if (paths == NULL || paths->count > MH_HALO_TEXTURES ||
		index >= paths->count || paths->size >= sizeof(paths->names))
		return NULL;

	offset = 0;

	for (i = 0; i <= index; i++) {
		end = memchr(paths->names + offset, 0, paths->size - offset);

		if (end == NULL || end == paths->names + offset)
			return NULL;

		if (i == index)
			return paths->names + offset;

		offset = (size_t)(end - paths->names) + 1;
	}

	return NULL;
}
