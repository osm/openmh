#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/video/divi_provider.h"
#include "engine/assets/divi.h"
#include "engine/core/binary.h"
#include "interfaces/video/source.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "video source:%d: %s\n", __LINE__,     \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	const struct mh_video_source_provider *clips;
	const struct mh_resource_source *resources;
	struct mh_resource_entry entries[2];
	struct mh_file file, before;
	unsigned char archive[MH_VIDEO_DIVI_HEADER_SIZE + 8];
	char selection[128], missing[128];
	int found;

	memset(archive, 0, sizeof(archive));
	memcpy(archive, "DIVI", 4);
	mh_write_u32le(archive + 4, 2);
	mh_write_u32le(archive + 8, MH_VIDEO_DIVI_HEADER_SIZE);
	mh_write_u32le(archive + 16, MH_VIDEO_DIVI_HEADER_SIZE);
	mh_write_u32le(archive + 28, MH_VIDEO_DIVI_HEADER_SIZE + 4);
	memcpy(archive + MH_VIDEO_DIVI_HEADER_SIZE, "ABCD1234", 8);
	entries[0].path = "library/Movies/MOVIES.PAK";
	entries[0].data = archive;
	entries[0].size = sizeof(archive);
	entries[1].path = "library/standalone.clip";
	entries[1].data = (const unsigned char *)"CLIP";
	entries[1].size = 4;
	CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
		MH_ERROR_NONE);
	clips = mh_video_divi_source();
	CHECK(mh_video_source_resolve(clips, resources, "library",
		"movies/movies.pak", selection, sizeof(selection), &found,
		NULL) == MH_ERROR_NONE);
	CHECK(found && strcmp(selection, entries[0].path) == 0);
	CHECK(mh_video_source_read(clips, resources, selection, 1, 1, &file,
		NULL) == MH_ERROR_NONE);
	CHECK(file.size == 4 && memcmp(file.data, "1234", 4) == 0);
	before = file;
	CHECK(mh_video_source_read(clips, resources, selection, 1, 2, &file,
		NULL) == MH_ERROR_INVALID);
	CHECK(file.data == before.data && file.size == before.size);
	free(file.data);
	CHECK(mh_video_source_read(clips, resources, entries[1].path, 0, 0,
		&file, NULL) == MH_ERROR_NONE);
	CHECK(file.size == 4 && memcmp(file.data, "CLIP", 4) == 0);
	free(file.data);
	strcpy(missing, "unchanged");
	CHECK(mh_video_source_resolve(clips, resources, "library",
		"movies/missing.pak", missing, sizeof(missing), &found,
		NULL) == MH_ERROR_NONE);
	CHECK(!found && strcmp(missing, "unchanged") == 0);
	found = 99;
	CHECK(mh_video_source_resolve(clips, resources, "library",
		"movies/movies.pak", missing, 1, &found,
		NULL) == MH_ERROR_RANGE);
	CHECK(found == 99 && strcmp(missing, "unchanged") == 0);
	CHECK(mh_video_source_resolve(NULL, resources, "library",
		"movies/movies.pak", missing, sizeof(missing), &found,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(found == 99 && strcmp(missing, "unchanged") == 0);
	mh_memory_resources_close(resources);
	puts("Video collections resolve and read independently of codecs and presentation");

	return 0;
}
