#include <stdio.h>
#include <string.h>

#include "engine/archive/path.h"
#include "interfaces/archive/archive_backend.h"

static int failures;

struct unsafe_path {
	const char	*directory, *name;
};

struct fixture {
	struct mh_archive_entry	entry;
	int			reads, closed;
};

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static void
check_paths(void)
{
	static const struct unsafe_path unsafe[] = {{"../outside", "file"},
		{"C:\\outside", "file"}, {"", "\\absolute"},
		{"safe", "..\\outside"}, {"/absolute", "file"},
		{"safe/./nested", "file"}, {"safe", ".."},
		{"safe//nested", "file"}, {"safe", "drive:stream"},
		{"safe", ""}};

	char path[MH_ARCHIVE_PATH_SIZE];
	struct mh_error error;
	size_t i;

	check(mh_archive_path("cars\\car01", "body.myo", path, &error) ==
				MH_ERROR_NONE &&
			strcmp(path, "cars/car01/body.myo") == 0,
		"DOS directories become relative portable paths");
	check(mh_archive_path("", "motor.exe", path, &error) == MH_ERROR_NONE &&
			strcmp(path, "motor.exe") == 0,
		"root member has no leading slash");

	for (i = 0; i < sizeof(unsafe) / sizeof(unsafe[0]); i++) {
		strcpy(path, "unchanged");
		check(mh_archive_path(unsafe[i].directory, unsafe[i].name, path,
			&error) == MH_ERROR_INVALID &&
				strcmp(path, "unchanged") == 0,
			"unsafe archive path rejected before output changes");
	}
}

static size_t
entry_count(const void *context)
{
	(void)context;

	return 1;
}

static const struct mh_archive_entry *
entry_info(const void *context, size_t index)
{
	const struct fixture *fixture;

	(void)index;
	fixture = context;

	return &fixture->entry;
}

static enum mh_error_code
read_entry(void *context, size_t index, struct mh_file *output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)index;
	(void)output;
	fixture = context;
	fixture->reads++;

	return mh_error_set(error_out, MH_ERROR_IO, "fixture decode failure");
}

static void
close_fixture(void *context)
{
	struct fixture *fixture;

	fixture = context;
	fixture->closed++;
}

static void
check_dispatch(void)
{
	static const struct mh_archive_ops ops = {
		entry_count, entry_info, read_entry, close_fixture};

	struct fixture fixture;
	struct mh_archive *archive;
	struct mh_file file;
	struct mh_error error;

	memset(&fixture, 0, sizeof(fixture));
	strcpy(fixture.entry.path, "Data/file");
	fixture.entry.size = 100;
	check(mh_archive_create(&ops, &fixture, &archive, &error) ==
			MH_ERROR_NONE,
		"open alternate archive provider");

	if (failures != 0)
		return;

	file.data = NULL;
	file.size = 17;
	check(mh_archive_count(archive) == 1 &&
			mh_archive_entry(archive, 0) == &fixture.entry &&
			mh_archive_entry(archive, 1) == NULL,
		"bounded archive entry lookup");
	check(mh_archive_read(archive, 0, 99, &file, &error) ==
				MH_ERROR_RANGE &&
			fixture.reads == 0 && file.data == NULL &&
			file.size == 17,
		"allocation limit rejected before backend decode");
	check(mh_archive_read(archive, 1, 100, &file, &error) ==
				MH_ERROR_INVALID &&
			fixture.reads == 0,
		"invalid index rejected before backend decode");
	check(mh_archive_read(archive, 0, 100, &file, &error) == MH_ERROR_IO &&
			fixture.reads == 1 && file.data == NULL &&
			file.size == 17,
		"backend failure preserves output");
	mh_archive_close(archive);
	check(fixture.closed == 1, "archive close releases provider once");
	mh_archive_close(NULL);
}

int
main(void)
{
	check_paths();
	check_dispatch();

	return failures != 0;
}
