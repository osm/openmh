#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "client/diagnostics/storage_recording_path.h"
#include "engine/core/error.h"
#include "engine/storage/recording_path.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr,                                        \
				"network recording path line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const char *invalid[] = {"", "demos\\.mde", "other\\name.mde",
		"demos\\../escape.mde", "demos\\..\\escape.mde",
		"demos\\name:.mde", "demos\\name .mde", "demos\\name..mde",
		"demos\\a\nb.mde"};

	char *directory, command[76], path[1024], before[1024];
	unsigned int i;

	CHECK((directory = mh_test_directory("mh-network-path-")) != NULL);
	memset(path, 0xa5, sizeof(path));
	memcpy(before, path, sizeof(path));

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(mh_recording_network_path(directory, invalid[i], path,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(before, path, sizeof(path)) == 0);
	}

	strcpy(command, "demos\\");
	memset(command + 6, 'a', 64);
	strcpy(command + 70, ".mde");
	CHECK(mh_recording_network_path(directory, command, path, NULL) ==
		MH_ERROR_NONE);
	CHECK(strncmp(path, directory, strlen(directory)) == 0 &&
		strlen(path) == strlen(directory) + 69);
	CHECK(strcmp(path + strlen(path) - 4, ".mde") == 0);
	memcpy(before, path, sizeof(path));
	strcpy(command + 71, ".mde");
	command[70] = 'a';
	CHECK(mh_recording_network_path(directory, command, path, NULL) !=
			MH_ERROR_NONE &&
		memcmp(before, path, sizeof(path)) == 0);
	CHECK(mh_recording_network_path(directory, "demos\\Bj\xf6rn \xc5.mde",
		path, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path + strlen(directory), "/bj\xf6rn \xc5.mde") == 0);
	CHECK(rmdir(directory) == 0);
	puts("Network recording paths: original 64-byte names, byte preservation, traversal/invalid-name rejection pass");

	return 0;
}
