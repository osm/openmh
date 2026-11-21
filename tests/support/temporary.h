#ifndef MH_TEST_TEMPORARY_H
#define MH_TEST_TEMPORARY_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * POSIX filesystem fixtures create one directory per process. Keep its
 * name short enough for their bounded child paths, and honor TMPDIR rather
 * than assuming that /tmp has free space. The caller removes its contents.
 * The returned path is borrowed until the next call.
 */
static char *
mh_test_directory(const char *name)
{
	static char path[96];
	const char *root;
	size_t root_length, name_length;

	root = getenv("TMPDIR");

	if (root == NULL || root[0] == '\0')
		root = "/tmp";

	root_length = strlen(root);
	name_length = strlen(name);

	/* Slash, six mkdtemp Xs and NUL require eight extra bytes. */
	if (root[0] != '/' || root_length >= sizeof(path) ||
		name_length >= sizeof(path) - root_length ||
		root_length + name_length + 8 > sizeof(path)) {
		fprintf(stderr,
			"Test TMPDIR must be absolute and leave room for %sXXXXXX in %lu bytes\n",
			name, (unsigned long)sizeof(path));
		errno = ENAMETOOLONG;
		return NULL;
	}

	memcpy(path, root, root_length);
	path[root_length++] = '/';
	memcpy(path + root_length, name, name_length);
	memcpy(path + root_length + name_length, "XXXXXX", 7);

	if (mkdtemp(path) == NULL) {
		perror(path);
		return NULL;
	}

	return path;
}

#endif
