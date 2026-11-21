#define _GNU_SOURCE
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/filesystem_staging.h"

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static char *
join(const char *root, const char *name)
{
	char *path;
	size_t length;

	length = strlen(root);
	path = malloc(length + strlen(name) + 2);

	if (path == NULL)
		exit(1);

	memcpy(path, root, length);
	path[length] = '/';
	strcpy(path + length + 1, name);

	return path;
}

static void
check_transaction(const char *root, int publish, int collision)
{
	struct mh_filesystem_staging *staging;
	struct mh_error error;
	struct stat info;
	char *destination, *temporary, *nested, *file;
	FILE *stream;

	destination = join(root, "output");
	check(mh_filesystem_staging_open(destination, &staging, &error) ==
			MH_ERROR_NONE,
		"create private staging directory");

	if (failures != 0) {
		free(destination);
		return;
	}

	temporary = strdup(mh_filesystem_staging_path(staging));
	check(strncmp(strrchr(temporary, '/') + 1, "openmotorex_tmp_",
		sizeof("openmotorex_tmp_") - 1) == 0,
		"staging name is visible and identifies the extractor");
	nested = join(temporary, "game");
	file = join(nested, "data");
	check(mkdir(nested, 0700) == 0, "create staged game directory");
	stream = fopen(file, "wb");
	check(stream != NULL, "create staged member");

	if (stream != NULL) {
		check(fputs("original bytes", stream) >= 0,
			"write staged member");
		check(fclose(stream) == 0, "close staged member");
	}

	check(lstat(destination, &info) != 0,
		"destination invisible before publication");

	if (collision) {
		check(mkdir(destination, 0700) == 0,
			"create competing destination");
		check(mh_filesystem_staging_publish(staging, &error) ==
				MH_ERROR_EXISTS,
			"publication cannot replace a competing directory");
	} else if (publish) {
		check(mh_filesystem_staging_publish(staging, &error) ==
				MH_ERROR_NONE,
			"publish complete tree");
		check(mh_filesystem_staging_publish(staging, &error) ==
				MH_ERROR_STATE,
			"publication cannot repeat");
	}

	mh_filesystem_staging_close(staging);
	check(lstat(temporary, &info) != 0, "staging tree removed after close");
	free(file);
	free(nested);
	free(temporary);

	if (publish && !collision) {
		char bytes[32];
		size_t count;

		nested = join(destination, "game");
		file = join(nested, "data");
		stream = fopen(file, "rb");
		check(stream != NULL, "published member exists");

		if (stream != NULL) {
			count = fread(bytes, 1, sizeof(bytes), stream);
			check(count == strlen("original bytes") &&
					memcmp(bytes, "original bytes",
						count) == 0,
				"publication preserves member contents");
			fclose(stream);
		}

		unlink(file);
		rmdir(nested);
		free(file);
		free(nested);
	} else if (!collision)
		check(lstat(destination, &info) != 0,
			"abort leaves no destination");

	if (publish || collision)
		check(rmdir(destination) == 0,
			"final destination preserved and removable");

	free(destination);
}

static void
check_existing(const char *root)
{
	struct mh_filesystem_staging *staging;
	struct mh_error error;
	char *path;

	path = join(root, "link");
	check(symlink("missing-target", path) == 0,
		"create dangling destination link");
	staging = NULL;
	check(mh_filesystem_staging_open(path, &staging, &error) ==
				MH_ERROR_EXISTS &&
			staging == NULL,
		"dangling symlink is an existing destination");
	check(unlink(path) == 0, "existing symlink preserved");
	free(path);
}

static void
check_scratch(const char *root)
{
	struct mh_filesystem_staging *staging;
	struct stat info;
	char *sibling, *temporary;
	FILE *stream;

	sibling = join(root, "game.ass");
	stream = fopen(sibling, "wb");
	check(stream != NULL, "create existing package");

	if (stream == NULL) {
		free(sibling);
		return;
	}

	fputs("package", stream);
	fclose(stream);
	staging = NULL;
	check(mh_filesystem_staging_scratch(sibling, &staging, NULL) ==
			MH_ERROR_NONE,
		"scratch workspace beside an existing file");

	if (staging != NULL) {
		temporary = strdup(mh_filesystem_staging_path(staging));
		check(mh_filesystem_staging_publish(staging, NULL) ==
				MH_ERROR_STATE,
			"scratch workspace cannot replace its sibling");
		mh_filesystem_staging_close(staging);
		check(lstat(temporary, &info) != 0,
			"scratch workspace removed");
		free(temporary);
	}

	check(stat(sibling, &info) == 0 && info.st_size == 7,
		"scratch leaves existing sibling unchanged");
	unlink(sibling);
	free(sibling);
}

static void
check_removal(const char *root)
{
	struct mh_filesystem_staging *staging;
	char *destination, *nested, *link, *outside;
	FILE *stream;

	destination = join(root, "pruned");
	outside = join(root, "outside");
	stream = fopen(outside, "wb");
	check(stream != NULL, "create protected sibling");

	if (stream != NULL)
		fclose(stream);

	check(mh_filesystem_staging_open(destination, &staging, NULL) ==
			MH_ERROR_NONE,
		"open pruning workspace");
	nested = join(mh_filesystem_staging_path(staging), "original");
	link = join(nested, "link");
	check(mkdir(nested, 0700) == 0, "create pruning subtree");
	check(symlink(root, link) == 0, "link subtree to protected parent");
	check(mh_filesystem_staging_remove(staging, "../outside", NULL) ==
			MH_ERROR_INVALID,
		"reject traversal during pruning");
	check(mh_filesystem_staging_remove(staging, ".", NULL) ==
			MH_ERROR_INVALID,
		"reject pruning workspace itself");
	check(mh_filesystem_staging_remove(staging, "original", NULL) ==
			MH_ERROR_NONE,
		"prune subtree without following its link");
	check(access(nested, F_OK) != 0 && access(outside, F_OK) == 0,
		"pruning leaves the linked parent untouched");
	check(mh_filesystem_staging_remove(staging, "original", NULL) ==
			MH_ERROR_NONE,
		"missing staged entry is already removed");
	check(mh_filesystem_staging_publish(staging, NULL) == MH_ERROR_NONE,
		"publish pruned workspace");
	check(mh_filesystem_staging_remove(staging, "missing", NULL) ==
			MH_ERROR_STATE,
		"published output cannot be pruned");
	mh_filesystem_staging_close(staging);
	check(rmdir(destination) == 0 && unlink(outside) == 0,
		"remove pruning fixture");
	free(destination);
	free(outside);
	free(nested);
	free(link);
}

int
main(void)
{
	char root[] = "/tmp/mh-staging-XXXXXX";

	if (mkdtemp(root) == NULL)
		return 1;

	check_transaction(root, 0, 0);
	check_transaction(root, 1, 0);
	check_transaction(root, 0, 1);
	check_existing(root);
	check_scratch(root);
	check_removal(root);
	check(rmdir(root) == 0, "test leaves no staging residue");

	return failures != 0;
}
