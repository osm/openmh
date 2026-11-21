#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/storage/path_names.h"
#include "engine/ui/menu_demo_catalog.h"

#define DEMO_EXTENSION_LENGTH (sizeof(MH_RECORDING_EXTENSION) - 1)
#define DEMO_FILENAME_SIZE (MH_MENU_DEMO_NAME_SIZE + DEMO_EXTENSION_LENGTH)

void
mh_menu_demo_catalog_free(struct mh_menu_demo_catalog *catalog)
{
	if (catalog == NULL)
		return;

	free(catalog->names);
	memset(catalog, 0, sizeof(*catalog));
}

static enum mh_error_code
read_basename(const char *filename, char *name, struct mh_error *error_out)
{
	unsigned int length;

	length = 0;

	if (filename != NULL)
		while (length < DEMO_FILENAME_SIZE && filename[length] != 0)
			length++;

	if (length < DEMO_EXTENSION_LENGTH + 1 ||
		length == DEMO_FILENAME_SIZE ||
		!mh_ascii_equal(filename + length - DEMO_EXTENSION_LENGTH,
			MH_RECORDING_EXTENSION) ||
		strpbrk(filename, "/\\") != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo catalog filename");

	/* The zero-filled catalog slot supplies the terminator. */
	memcpy(name, filename, length - DEMO_EXTENSION_LENGTH);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_demo_catalog_read(const char *const *files, unsigned int count,
	const char *saved, struct mh_menu_demo_catalog *output,
	struct mh_error *error_out)
{
	struct mh_menu_demo_catalog parsed;
	enum mh_error_code status;
	unsigned int i, length;

	if (output == NULL || saved == NULL || count > MH_MENU_DEMO_MAX_FILES ||
		(count != 0 && files == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo menu catalog");

	for (length = 0; length < MH_MENU_DEMO_NAME_SIZE && saved[length] != 0;
		length++) {
	}

	if (length == MH_MENU_DEMO_NAME_SIZE)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"saved demo name is too long");

	memset(&parsed, 0, sizeof(parsed));
	parsed.count = count;

	if (count != 0) {
		parsed.names = (char (*)[MH_MENU_DEMO_NAME_SIZE])calloc(
			count, sizeof(*parsed.names));

		if (parsed.names == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory reading demo menu catalog");
	}

	for (i = 0; i < count; i++) {
		status = read_basename(files[i], parsed.names[i], error_out);

		if (status != MH_ERROR_NONE) {
			mh_menu_demo_catalog_free(&parsed);
			return status;
		}

		if (mh_ascii_equal(parsed.names[i], saved))
			parsed.selected = i;
	}

	*output = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
