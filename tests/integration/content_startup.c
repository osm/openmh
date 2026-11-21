#include <stdio.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "client/application/application_internal.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "content startup line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_arguments(void)
{
	struct mh_application app;
	char *args[] = {"openmotor", "--assets", "chosen-game"};

	memset(&app, 0, sizeof(app));
	strcpy(app.game_directory, "default-game");
	CHECK(mh_application_parse_arguments(&app, 3, args) < 0);
	CHECK(strcmp(app.asset_root, "chosen-game") == 0);
	CHECK(mh_application_parse_arguments(&app, 2, args) == 1);
	args[2] = "chosen-demo.ass";
	CHECK(mh_application_parse_arguments(&app, 3, args) < 0);
	CHECK(strcmp(app.asset_root, "chosen-demo.ass") == 0);
	args[1] = "--gamedir";
	CHECK(mh_application_parse_arguments(&app, 3, args) == 1);
	args[1] = "--disc";
	CHECK(mh_application_parse_arguments(&app, 3, args) == 1);
	args[1] = "--music-assets";
	CHECK(mh_application_parse_arguments(&app, 3, args) == 1);

	return 0;
}

static int
check_missing(void)
{
	const struct mh_resource_source *source;
	struct mh_resource_entry entry;
	struct mh_application app;

	entry.path = "missing/unrelated.txt";
	entry.data = (const unsigned char *)"";
	entry.size = 0;
	CHECK(mh_memory_resources_open(&entry, 1, &source, NULL) ==
		MH_ERROR_NONE);
	memset(&app, 0, sizeof(app));
	app.resources = source;
	app.asset_root = "missing";
	CHECK(mh_application_load_content(&app, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(app.content_edition == NULL);
	CHECK(app.installation_root == NULL);
	CHECK(app.asset_mount == NULL && app.resources == source);
	mh_memory_resources_close(source);

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_application app;
	char *args[] = {"openmotor", "--assets", NULL};

	if (check_arguments() != 0)
		return 1;

	if (argc == 1)
		return check_missing();

	CHECK(argc == 3);
	CHECK(strlen(argv[1]) < sizeof(app.game_directory));
	args[2] = argv[1];
	memset(&app, 0, sizeof(app));
	app.resources = mh_filesystem_resources();
	strcpy(app.game_directory, argv[1]);
	CHECK(mh_application_parse_arguments(&app, 1, args) < 0);
	CHECK(mh_application_load_content(&app, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(app.asset_root, argv[1]) == 0);
	CHECK(strcmp(app.content_edition->id, argv[2]) == 0);
	strcpy(app.game_directory, "default-is-overridden");
	CHECK(mh_application_parse_arguments(&app, 3, args) < 0);
	CHECK(mh_application_load_content(&app, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(app.asset_root, argv[1]) == 0);

	/* NULL delegates the ordinary Music/ default to the music owner. */
	CHECK(app.session_options.music_root == NULL);
	mh_vfs_close(app.asset_mount);

	return 0;
}
