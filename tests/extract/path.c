#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extract/path/join.h"
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "path:%d: %s\n", __LINE__, #test);     \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
join(const char *root, const char *name, const char *expected)
{
	char *path = NULL;

	CHECK(mh_extract_join(root, name, &path, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, expected) == 0);
	free(path);

	return 0;
}

int
main(void)
{
	const char prefix[3] = {'d', ':', '/'};
	char sentinel, *path = &sentinel;

	CHECK(join("", "disc.bin", "disc.bin") == 0);
	CHECK(join("/", "disc.bin", "/disc.bin") == 0);
	CHECK(join("root", "nested/file", "root/nested/file") == 0);
	CHECK(join("root/", "file", "root/file") == 0);
	CHECK(join("//server/share", "file", "//server/share/file") == 0);
	CHECK(join("root", "", "root/") == 0);
	CHECK(mh_extract_join(NULL, "file", &path, NULL) == MH_ERROR_INVALID);
	CHECK(path == &sentinel);
	CHECK(mh_extract_join_prefix("root", (size_t)-1, "file", &path, NULL) ==
		MH_ERROR_RANGE);
	CHECK(path == &sentinel);
	CHECK(mh_extract_join_prefix(prefix, sizeof(prefix), "file", &path,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "d:/file") == 0);
	free(path);

	return 0;
}
