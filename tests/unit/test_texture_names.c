#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/texture_names.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "texture names:%d: %s\n", __LINE__,    \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	unsigned char data[] = "road\0\0sky";
	unsigned char truncated[] = {'a', 0, 'b'};
	char *names[3] = {NULL, NULL, NULL};
	struct mh_error error;
	unsigned int i;

	CHECK(mh_texture_names_copy(data, sizeof(data), 3, names,
		"allocation failed", &error) == MH_ERROR_NONE);
	CHECK(strcmp(names[0], "road") == 0);
	CHECK(strcmp(names[1], "") == 0);
	CHECK(strcmp(names[2], "sky") == 0);
	memset(data, 'x', sizeof(data));
	CHECK(strcmp(names[0], "road") == 0 && strcmp(names[2], "sky") == 0);

	for (i = 0; i < 3; i++) {
		free(names[i]);
		names[i] = NULL;
	}

	CHECK(mh_texture_names_copy(truncated, sizeof(truncated), 2, names,
		"allocation failed", &error) == MH_ERROR_INVALID);
	CHECK(error.code == MH_ERROR_INVALID && names[1] == NULL);
	CHECK(strcmp(names[0], "a") == 0);
	free(names[0]);
	names[0] = NULL;
	CHECK(mh_texture_names_copy(truncated, 0, 1, names, "allocation failed",
		NULL) == MH_ERROR_INVALID);
	CHECK(names[0] == NULL);
	CHECK(mh_texture_names_copy(NULL, 0, 0, NULL, "allocation failed",
		&error) == MH_ERROR_NONE);
	CHECK(error.code == MH_ERROR_NONE && error.message == NULL);
	puts("Texture names: bounded copies, independent ownership and partial failure passed");

	return 0;
}
