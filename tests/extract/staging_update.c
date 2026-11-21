#define _XOPEN_SOURCE 700
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/filesystem_staging.h"
#include "backends/storage/filesystem_tree.h"

static int failures;

static void
check(int success, const char *message)
{
	if (!success) {
		fprintf(stderr, "%s\n", message);
		failures++;
	}
}

static void
write_text(const char *root, const char *name, const char *text)
{
	char path[1024];
	FILE *stream;

	if (strlen(root) + strlen(name) + 2 > sizeof(path))
		exit(1);

	strcpy(path, root);
	strcat(path, "/");
	strcat(path, name);
	stream = fopen(path, "wb");

	if (stream == NULL)
		exit(1);

	check(fputs(text, stream) >= 0, "write fixture");
	check(fclose(stream) == 0, "close fixture");
}

static void
check_text(const char *path, const char *expected)
{
	char text[64];
	FILE *stream;
	size_t size;

	stream = fopen(path, "rb");
	check(stream != NULL, "fixture exists");

	if (stream == NULL)
		return;

	size = fread(text, 1, sizeof(text), stream);
	check(size == strlen(expected) && memcmp(text, expected, size) == 0,
		"fixture bytes preserved");
	fclose(stream);
}

static enum mh_error_code
cancel_copy(void *context, const char *name, struct mh_error *error_out)
{
	(void)context;
	(void)name;

	return mh_error_set(error_out, MH_ERROR_FAILED, "test cancellation");
}

static void
check_update(const char *destination, int publish, int cancel)
{
	struct mh_filesystem_staging *staging;
	struct mh_error error;
	char *temporary;
	enum mh_error_code status;

	staging = NULL;
	status = mh_filesystem_staging_update(destination, &staging, &error);
	check(status == MH_ERROR_NONE, "open replacement transaction");

	if (status != MH_ERROR_NONE)
		return;

	temporary = strdup(mh_filesystem_staging_path(staging));

	if (temporary == NULL)
		exit(1);

	status = mh_filesystem_copy_tree(destination, temporary,
		cancel ? cancel_copy : NULL, NULL, &error);

	if (cancel)
		check(status == MH_ERROR_FAILED, "copy can be cancelled");
	else {
		check(status == MH_ERROR_NONE,
			"copy complete existing installation");
		write_text(temporary, "game.bin", "patched");

		if (publish)
			check(mh_filesystem_staging_publish(staging, &error) ==
					MH_ERROR_NONE,
				"publish complete replacement");
	}

	mh_filesystem_staging_close(staging);
	check(access(temporary, F_OK) != 0, "private or retired tree cleaned");
	free(temporary);
}

int
main(void)
{
	char root[] = "/tmp/mh-update-XXXXXX";
	char destination[1024], game[1024], saves[1024];

	if (mkdtemp(root) == NULL)
		return 1;

	strcpy(destination, root);
	strcat(destination, "/game");
	check(mkdir(destination, 0700) == 0, "create installation");
	strcpy(game, destination);
	strcat(game, "/game.bin");
	strcpy(saves, destination);
	strcat(saves, "/settings.cfg");
	write_text(destination, "game.bin", "original");
	write_text(destination, "settings.cfg", "user settings");
	check_update(destination, 0, 1);
	check_text(game, "original");
	check_update(destination, 0, 0);
	check_text(game, "original");
	check_update(destination, 1, 0);
	check_text(game, "patched");
	check_text(saves, "user settings");
	unlink(game);
	unlink(saves);
	check(rmdir(destination) == 0, "remove test installation");
	check(rmdir(root) == 0, "no private staging residue");

	return failures != 0;
}
