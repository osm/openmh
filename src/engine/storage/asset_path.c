#include <string.h>

#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

enum {
	ASSET_COMPONENT_SIZE = 256
};

struct path_match {
	const char	*wanted;
	char		name[ASSET_COMPONENT_SIZE];
	int		found;
};

size_t
mh_path_directory_length(const char *path)
{
	size_t length;

	length = strlen(path);

	while (length != 0 && path[length - 1] != '/' &&
		path[length - 1] != '\\')
		length--;

	return length;
}

static enum mh_directory_action
match_entry(void *context, const char *name)
{
	struct path_match *match;

	match = (struct path_match *)context;

	if (!mh_ascii_equal(match->wanted, name))
		return MH_DIRECTORY_CONTINUE;

	if (match->found ||
		mh_text_copy(match->name, sizeof(match->name), name, NULL) !=
			MH_ERROR_NONE)
		return MH_DIRECTORY_FAIL;

	match->found = 1;

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
validate_relative_path(const char *relative, size_t component_capacity,
	struct mh_error *error_out)
{
	size_t length;
	const char *scan, *start;

	if (*relative == 0 || *relative == '/' || *relative == '\\' ||
		strchr(relative, ':') != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected relative asset path");

	/* Validate the whole name even if an early component is missing. */
	scan = relative;

	while (*scan != 0) {
		start = scan;

		while (*scan != 0 && *scan != '/' && *scan != '\\')
			scan++;

		length = (size_t)(scan - start);

		if (length == 0 || (length == 1 && start[0] == '.') ||
			(length == 2 && start[0] == '.' && start[1] == '.'))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid asset component");

		if (length >= component_capacity)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"asset component too long");

		if (*scan != 0)
			scan++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_asset_path_find_from(const struct mh_resource_source *source,
	const char *root, const char *relative, char *resolved, size_t capacity,
	int *found, struct mh_error *error_out)
{
	struct path_match match;
	char component[ASSET_COMPONENT_SIZE];
	size_t used, length;
	enum mh_error_code status;

	if (source == NULL || root == NULL || relative == NULL ||
		resolved == NULL || found == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing asset lookup result");

	*found = 0;
	used = strlen(root);

	if (used == 0 || used >= capacity)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"asset root too long or empty");

	status = validate_relative_path(relative, sizeof(component), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(resolved, root, used + 1);

	while (*relative != 0) {
		length = 0;

		while (*relative != 0 && *relative != '/' &&
			*relative != '\\') {
			if (length == sizeof(component) - 1)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"asset component too long");

			component[length++] = *relative++;
		}

		component[length] = 0;

		if (length == 0 || strcmp(component, ".") == 0 ||
			strcmp(component, "..") == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid asset component");

		match.wanted = component;
		match.found = 0;

		if (mh_resource_source_list(source, resolved, match_entry,
			&match, NULL) != MH_ERROR_NONE) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"cannot enumerate asset directory or ambiguous filename");
		}

		if (!match.found)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (used >= capacity - 1 || length >= capacity - used - 1) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"resolved asset path too long");
		}

		resolved[used++] = '/';
		memcpy(resolved + used, match.name, length + 1);
		used += length;

		if (*relative != 0)
			relative++;
	}

	*found = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_asset_path_from(const struct mh_resource_source *source, const char *root,
	const char *relative, char *resolved, size_t capacity,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	int found;

	status = mh_asset_path_find_from(
		source, root, relative, resolved, capacity, &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!found)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"asset path component not found");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_asset_reference_extension(const char *reference, const char *extension,
	char *output, size_t capacity, struct mh_error *error_out)
{
	const char *scan, *suffix, *basename;
	size_t stem, length;

	if (reference == NULL || extension == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing asset reference");

	basename = reference;
	suffix = NULL;

	for (scan = reference; *scan != 0; scan++) {
		if (*scan == '/' || *scan == '\\') {
			basename = scan + 1;
			suffix = NULL;
		} else if (*scan == '.')
			suffix = scan;
	}

	if (*basename == 0 || suffix == basename ||
		(suffix != NULL && suffix[1] == 0))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid asset reference");

	stem = (size_t)((suffix == NULL ? scan : suffix) - reference);
	length = strlen(extension);

	if (length >= capacity || stem >= capacity - length - 1)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"asset reference is too long");

	memcpy(output, reference, stem);
	output[stem] = '.';
	memcpy(output + stem + 1, extension, length + 1);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
