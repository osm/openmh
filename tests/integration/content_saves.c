#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "client/application/application_internal.h"
#include "engine/storage/user_root.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "content saves line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_value(
	const struct mh_user_data *data, const char *path, const char *value)
{
	struct mh_file file;
	int equal;

	CHECK(mh_user_data_read(data, path, 256, &file, NULL) == MH_ERROR_NONE);
	equal = file.size == strlen(value) &&
		memcmp(file.data, value, file.size) == 0;
	free(file.data);
	CHECK(equal);

	return 0;
}

static enum mh_directory_action
visit_name(void *context, const char *name)
{
	unsigned int *count;

	count = context;

	if (strcmp(name, "profile.clo") != 0)
		return MH_DIRECTORY_FAIL;

	(*count)++;

	return MH_DIRECTORY_CONTINUE;
}

static int
check_directories(const struct mh_user_data *data, const char *root)
{
	struct mh_data_directory *directory, *browse;
	struct mh_data_file *file;
	char path[1024];
	unsigned int count;
	int same;

	sprintf(path, "%s/Controls", root);
	CHECK(mh_user_data_create_directory(data, path, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_check_directory(data, path, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(
		data, path, "assets", &directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_create(directory, "profile.clo", "layout", 6,
		NULL, NULL, &file, NULL) == MH_ERROR_NONE);
	mh_data_file_close(file);
	count = 0;
	CHECK(mh_user_data_list(data, path, visit_name, &count, NULL) ==
		MH_ERROR_NONE);
	CHECK(count == 1);
	CHECK(mh_user_data_browse_directory(
		data, path, "assets", &browse, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_same(directory, browse, &same, NULL) ==
			MH_ERROR_NONE &&
		same);
	mh_data_directory_close(browse);
	mh_data_directory_close(directory);

	return 0;
}

static int
check_edition(const struct mh_user_data *source, const char *id, int reload)
{
	struct mh_application app;
	const struct mh_user_data *selected;
	char *root, expected[256], path[1024];

	memset(&app, 0, sizeof(app));
	app.user_data = source;
	app.content_edition = mh_content_edition_find(id);
	sprintf(expected, "installs/%s", id);
	app.installation_root = expected;
	app.asset_root = "assets";
	app.session_options.screenshot_data = source;
	CHECK(app.content_edition != NULL);
	CHECK(mh_application_content_saves(&app, NULL) == MH_ERROR_NONE);
	selected = app.content_user_data;
	CHECK(selected != NULL && app.user_data == selected &&
		app.session_options.user_data == selected &&
		app.session_options.screenshot_data == selected);
	CHECK(mh_application_content_saves(&app, NULL) == MH_ERROR_NONE &&
		app.content_user_data == selected);
	sprintf(expected, "installs/%s/userdata", id);

	if (!reload)
		CHECK(mh_user_data_check_directory(source, expected, NULL) ==
			MH_ERROR_NOT_FOUND);

	CHECK(mh_user_data_directory(selected, &root, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(root, expected) == 0);
	sprintf(path, "%s/MotorHead.CFG", root);

	if (reload)
		CHECK(check_value(selected, path, id) == 0);
	else
		CHECK(mh_user_data_replace(selected, path, id, strlen(id),
			NULL) == MH_ERROR_NONE);

	sprintf(path, "%s/Atlantika.rec", root);

	if (reload)
		CHECK(check_value(selected, path, id) == 0);
	else {
		CHECK(mh_user_data_replace(selected, path, id, strlen(id),
			NULL) == MH_ERROR_NONE);
		CHECK(check_directories(selected, root) == 0);
	}

	CHECK(mh_user_data_replace(selected, "explicit.cfg", "override", 8,
		NULL) == MH_ERROR_NONE);
	CHECK(check_value(source, "explicit.cfg", "override") == 0);
	free(root);
	mh_user_root_close(selected);

	return 0;
}

static int
check_override(const struct mh_user_data *source)
{
	struct mh_application app;
	char *root;

	memset(&app, 0, sizeof(app));
	app.user_data = source;
	app.content_edition = mh_content_edition_find("retail-europe");
	app.installation_root = "packages";
	app.asset_root = "packages/retail.ass";
	app.user_data_directory = "profiles/Player One";
	app.session_options.screenshot_data = source;
	CHECK(mh_application_content_saves(&app, NULL) == MH_ERROR_NONE);
	CHECK(app.session_options.user_data == app.content_user_data);
	CHECK(app.session_options.screenshot_data == app.content_user_data);
	CHECK(mh_user_data_directory(app.user_data, &root, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(root, "profiles/Player One") == 0);
	CHECK(mh_user_data_check_directory(source, "packages/userdata", NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(mh_user_data_check_directory(source,
		"profiles/Player One/userdata",
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(check_directories(app.user_data, root) == 0);
	free(root);
	mh_user_root_close(app.content_user_data);

	return 0;
}

int
main(void)
{
	static const char *const ids[] = {"motordemo_soft",
		"motordemo_soft_intro", "motordemo2_soft", "motordemo_3dfx",
		"motordemo_3dfx_intro", "motordemo2_3dfx_intro",
		"motordemo_mplayer"};

	const struct mh_user_data *memory, *source;
	struct mh_application retail;
	unsigned int i;

	CHECK(mh_memory_user_data_open(&memory, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_root_open(memory, "Users/Tester", &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_replace(source, "Users/Tester/MotorHead.CFG",
		"retail", 6, NULL) == MH_ERROR_NONE);

	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
		CHECK(check_edition(source, ids[i], 0) == 0);

	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
		CHECK(check_edition(source, ids[i], 1) == 0);

	CHECK(check_value(source, "Users/Tester/MotorHead.CFG", "retail") == 0);
	CHECK(check_override(source) == 0);
	memset(&retail, 0, sizeof(retail));
	retail.content_edition = mh_content_edition_find("retail-europe");
	retail.user_data = source;
	retail.asset_root = "retail";
	CHECK(mh_application_content_saves(&retail, NULL) == MH_ERROR_NONE);
	CHECK(retail.content_user_data != NULL);
	{
		char *root;

		CHECK(mh_user_data_directory(retail.user_data, &root, NULL) ==
			MH_ERROR_NONE);
		CHECK(strcmp(root, "retail/userdata") == 0);
		free(root);
	}

	mh_user_root_close(retail.content_user_data);
	mh_user_root_close(source);
	mh_memory_user_data_close(memory);

	puts("Installation saves: demos and retail use local userdata, shared screenshots and explicit overrides");

	return 0;
}
