#include <stdio.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "client/race/internal.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "optional finish line %d: %s\n",       \
				__LINE__, #expression);                        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static struct mh_race_loop loop;
	static struct mh_race_context context;
	const struct mh_resource_source *source;
	struct mh_resource_entry entries[2];
	struct mh_error error;

	entries[0].path = "root/Data/placeholder";
	entries[0].data = (const unsigned char *)"";
	entries[0].size = 0;
	entries[1] = entries[0];
	entries[1].path = "root/Data/Finish/finish.mot";
	loop.context = &context;
	loop.root = "root";
	loop.finish_count = MH_FINISH_SIGNS;
	loop.finish_seen = 1;
	loop.finish_position = 1;
	loop.finish_seconds = 0.5f;
	CHECK(mh_memory_resources_open(entries, 1, &source, NULL) ==
		MH_ERROR_NONE);
	context.resources = source;
	CHECK(mh_race_load_finish(&loop, &error) == MH_ERROR_NONE);
	CHECK(loop.finish_count == 0 && error.code == MH_ERROR_NONE);

	/*
	 * A finisher without artwork needs neither motion nor renderer calls.
	 */
	CHECK(mh_race_prepare_finish(&loop));
	CHECK(loop.finish_seen == 1 && loop.finish_position == 1 &&
		loop.finish_seconds == 0.5f);
	CHECK(context.scene.assets.finish.motion.keys == NULL);
	mh_memory_resources_close(source);

	CHECK(mh_memory_resources_open(entries, 2, &source, NULL) ==
		MH_ERROR_NONE);
	context.resources = source;
	CHECK(mh_race_load_finish(&loop, &error) != MH_ERROR_NONE);
	CHECK(loop.finish_count == 0 &&
		context.scene.assets.finish.motion.keys == NULL);
	mh_memory_resources_close(source);

	/* An unreadable root is not an optional missing presentation pack. */
	CHECK(mh_memory_resources_open(NULL, 0, &source, NULL) ==
		MH_ERROR_NONE);
	context.resources = source;
	CHECK(mh_race_load_finish(&loop, &error) != MH_ERROR_NONE);
	mh_memory_resources_close(source);

	return 0;
}
