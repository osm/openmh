#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/assets/content_probe.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "content probe line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_unknown(void)
{
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *source;
	struct mh_content_info info, before;
	struct mh_error error;
	unsigned char *bytes;

	memset(&info, 0, sizeof(info));
	info.edition = mh_content_edition_find("motordemo_soft");
	before = info;
	entries[0].path = "missing/unrelated.txt";
	entries[0].data = (const unsigned char *)"";
	entries[0].size = 0;
	CHECK(mh_memory_resources_open(entries, 1, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_content_probe(source, "missing", NULL, &info, &error) ==
		MH_ERROR_NOT_FOUND);
	CHECK(strstr(error.message, "game executable") != NULL);
	CHECK(memcmp(&info, &before, sizeof(info)) == 0);
	CHECK(mh_content_probe(source, "", NULL, &info, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_content_probe(source, "missing", NULL, NULL, NULL) ==
		MH_ERROR_INVALID);
	mh_memory_resources_close(source);

	/*
	 * A manifest cannot turn an unknown executable into a supported game.
	 */
	entries[0].path = "game/manifest.txt";
	entries[0].data =
		(const unsigned char *)"MHCONTENT 1\nedition retail-europe\n";
	entries[0].size = strlen((const char *)entries[0].data);
	entries[1].path = "game/MOTOR.EXE";
	entries[1].data = (const unsigned char *)"unknown";
	entries[1].size = 7;
	CHECK(mh_memory_resources_open(entries, 2, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_content_probe(source, "game", NULL, &info, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&info, &before, sizeof(info)) == 0);
	mh_memory_resources_close(source);

	/* Matching the retail byte count alone must not identify it. */
	bytes = calloc(940544UL, 1);
	CHECK(bytes != NULL);
	entries[1].data = bytes;
	entries[1].size = 940544UL;
	CHECK(mh_memory_resources_open(entries, 2, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_content_probe(source, "game", NULL, &info, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&info, &before, sizeof(info)) == 0);
	mh_memory_resources_close(source);
	free(bytes);

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_content_info info;
	struct mh_error error;

	if (argc == 1)
		return check_unknown();

	CHECK(argc == 5);

	if (mh_content_probe(mh_filesystem_resources(), argv[1], NULL, &info,
		&error) != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	CHECK(strcmp(info.edition->id, argv[2]) == 0);
	CHECK((int)info.revision == atoi(argv[3]));
	CHECK(info.intro.count == (unsigned int)atoi(argv[4]));
	printf("%s: revision %d, intro %u\n", info.edition->id,
		(int)info.revision, info.intro.count);

	return 0;
}
