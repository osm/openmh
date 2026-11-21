#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "backends/storage/filesystem_staging.h"
#include "backends/storage/filesystem_tree.h"
#include "backends/storage/filesystem_windows_handle.h"

#define CHECK(condition)                                                         \
	do {                                                                     \
		if (!(condition)) {                                              \
			fprintf(stderr,                                          \
				"Windows staging line %d: %s (%s; Win32 %lu)\n", \
				__LINE__, #condition,                            \
				error.message == NULL ? "no diagnostic"          \
						      : error.message,           \
				(unsigned long)GetLastError());                  \
			exit(1);                                                 \
		}                                                                \
	} while (0)

static struct mh_error error;
static unsigned int rename_count, fail_from, fail_through;
static int crash_after_backup;

enum mh_error_code	__real_mh_windows_rename_handle(HANDLE, HANDLE,
	const WCHAR *, int, struct mh_error *);

enum mh_error_code
__wrap_mh_windows_rename_handle(HANDLE file, HANDLE directory,
	const WCHAR *path, int replace, struct mh_error *error_out)
{
	rename_count++;

	if (crash_after_backup && rename_count == 2)
		ExitProcess(17);

	if (rename_count >= fail_from && rename_count <= fail_through)
		return mh_error_set(
			error_out, MH_ERROR_IO, "fixture publication failure");

	return __real_mh_windows_rename_handle(
		file, directory, path, replace, error_out);
}

static WCHAR *
wide_path(const char *root, const char *name)
{
	char path[2048];
	WCHAR *wide;

	CHECK(strlen(root) + strlen(name) + 2 <= sizeof(path));
	strcpy(path, root);
	strcat(path, "/");
	strcat(path, name);
	CHECK(mh_windows_path_to_wide(path, &wide, &error) == MH_ERROR_NONE);

	return wide;
}

static void
write_text(const char *root, const char *name, const char *text)
{
	WCHAR *wide;
	HANDLE file;
	DWORD written;

	wide = wide_path(root, name);
	file = CreateFileW(
		wide, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	CHECK(file != INVALID_HANDLE_VALUE);
	free(wide);
	CHECK(WriteFile(file, text, (DWORD)strlen(text), &written, NULL));
	CHECK(written == strlen(text));
	CHECK(CloseHandle(file));
}

static void
check_text(const WCHAR *path, const char *expected)
{
	HANDLE file;
	char bytes[64];
	DWORD count;

	file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, 0, NULL);
	CHECK(file != INVALID_HANDLE_VALUE);
	CHECK(ReadFile(file, bytes, sizeof(bytes), &count, NULL));
	CHECK(count == strlen(expected) && memcmp(bytes, expected, count) == 0);
	CHECK(CloseHandle(file));
}

static struct mh_filesystem_staging *
prepare_update(void)
{
	struct mh_filesystem_staging *staging;

	CHECK(mh_filesystem_staging_update("Game", &staging, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_filesystem_copy_tree("Game",
		mh_filesystem_staging_path(staging), NULL, NULL,
		&error) == MH_ERROR_NONE);
	write_text(mh_filesystem_staging_path(staging), "value", "patched");
	rename_count = 0;

	return staging;
}

static void
check_base(void)
{
	struct mh_filesystem_staging *staging, *unchanged;
	WCHAR *private_path;

	CHECK(mh_filesystem_staging_open("Game", &staging, &error) ==
		MH_ERROR_NONE);
	write_text(mh_filesystem_staging_path(staging), "value", "original");
	private_path =
		wide_path(mh_filesystem_staging_path(staging), "Original");
	CHECK(CreateDirectoryW(private_path, NULL));
	write_text(mh_filesystem_staging_path(staging), "Original/installer",
		"source");
	CHECK(mh_filesystem_staging_remove(staging, "../Game", &error) ==
		MH_ERROR_INVALID);
	CHECK(mh_filesystem_staging_remove(staging, "Original", &error) ==
		MH_ERROR_NONE);
	CHECK(GetFileAttributesW(private_path) == INVALID_FILE_ATTRIBUTES);
	free(private_path);
	CHECK(mh_filesystem_staging_remove(staging, "Original", &error) ==
		MH_ERROR_NONE);
	CHECK(mh_windows_path_to_wide(mh_filesystem_staging_path(staging),
		&private_path, &error) == MH_ERROR_NONE);
	CHECK(wcsncmp(wcsrchr(private_path, L'\\') + 1, L"openmotorex_tmp_",
		wcslen(L"openmotorex_tmp_")) == 0);
	free(private_path);
	CHECK(mh_filesystem_staging_publish(staging, &error) == MH_ERROR_NONE);
	CHECK(mh_filesystem_staging_publish(staging, &error) == MH_ERROR_STATE);
	CHECK(mh_filesystem_staging_remove(staging, "value", &error) ==
		MH_ERROR_STATE);
	mh_filesystem_staging_close(staging);
	check_text(L"Game/value", "original");
	unchanged = NULL;
	CHECK(mh_filesystem_staging_open("Game", &unchanged, &error) ==
		MH_ERROR_EXISTS);
	CHECK(unchanged == NULL);

	CHECK(mh_filesystem_staging_open("Cancelled", &staging, &error) ==
		MH_ERROR_NONE);
	write_text(mh_filesystem_staging_path(staging), "value", "discard");
	CHECK(mh_windows_path_to_wide(mh_filesystem_staging_path(staging),
		&private_path, &error) == MH_ERROR_NONE);
	mh_filesystem_staging_close(staging);
	CHECK(GetFileAttributesW(private_path) == INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesW(L"Cancelled") == INVALID_FILE_ATTRIBUTES);
	free(private_path);

	CHECK(mh_filesystem_staging_open("Raced", &staging, &error) ==
		MH_ERROR_NONE);
	CHECK(CreateDirectoryW(L"Raced", NULL));
	write_text("Raced", "value", "external");
	CHECK(mh_filesystem_staging_publish(staging, &error) ==
		MH_ERROR_EXISTS);
	mh_filesystem_staging_close(staging);
	check_text(L"Raced/value", "external");
	CHECK(DeleteFileW(L"Raced/value"));
	CHECK(RemoveDirectoryW(L"Raced"));

	CHECK(mh_filesystem_staging_open("Game \303\245", &staging, &error) ==
		MH_ERROR_NONE);
	write_text(mh_filesystem_staging_path(staging), "value", "unicode");
	CHECK(mh_filesystem_staging_publish(staging, &error) == MH_ERROR_NONE);
	mh_filesystem_staging_close(staging);
	check_text(L"Game \x00e5/value", "unicode");
	CHECK(DeleteFileW(L"Game \x00e5/value"));
	CHECK(RemoveDirectoryW(L"Game \x00e5"));
}

static void
check_scratch(void)
{
	struct mh_filesystem_staging *staging;
	WCHAR *private_path;

	write_text(".", "game.ass", "package");
	CHECK(mh_filesystem_staging_scratch("game.ass", &staging, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_windows_path_to_wide(mh_filesystem_staging_path(staging),
		&private_path, &error) == MH_ERROR_NONE);
	CHECK(mh_filesystem_staging_publish(staging, &error) == MH_ERROR_STATE);
	mh_filesystem_staging_close(staging);
	CHECK(GetFileAttributesW(private_path) == INVALID_FILE_ATTRIBUTES);
	free(private_path);
	check_text(L"game.ass", "package");
	CHECK(DeleteFileW(L"game.ass"));
}

static void
check_update(void)
{
	struct mh_filesystem_staging *staging;
	HANDLE locked;

	CHECK(CreateDirectoryW(L"Game/userdata", NULL));
	write_text("Game/userdata", "save", "saved");
	staging = prepare_update();
	locked =
		CreateFileW(L"Game", FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, NULL);
	CHECK(locked != INVALID_HANDLE_VALUE);
	CHECK(mh_filesystem_staging_publish(staging, &error) != MH_ERROR_NONE);
	CHECK(CloseHandle(locked));
	mh_filesystem_staging_close(staging);
	check_text(L"Game/value", "original");
	CHECK(GetFileAttributesW(L"Game.openmotorex-backup") ==
		INVALID_FILE_ATTRIBUTES);

	staging = prepare_update();
	fail_from = fail_through = 2;
	CHECK(mh_filesystem_staging_publish(staging, &error) == MH_ERROR_IO);
	CHECK(strcmp(error.message, "fixture publication failure") == 0);
	CHECK(rename_count == 3);
	mh_filesystem_staging_close(staging);
	check_text(L"Game/value", "original");
	CHECK(GetFileAttributesW(L"Game.openmotorex-backup") ==
		INVALID_FILE_ATTRIBUTES);

	fail_from = fail_through = 0;
	staging = prepare_update();
	CHECK(mh_filesystem_staging_publish(staging, &error) == MH_ERROR_NONE);
	mh_filesystem_staging_close(staging);
	check_text(L"Game/value", "patched");
	check_text(L"Game/userdata/save", "saved");
	CHECK(GetFileAttributesW(L"Game.openmotorex-backup") ==
		INVALID_FILE_ATTRIBUTES);
}

static void
check_recovery(void)
{
	struct mh_filesystem_staging *staging, *blocked;

	write_text("Game", "value", "recover me");
	staging = prepare_update();
	fail_from = 2;
	fail_through = 3;
	CHECK(mh_filesystem_staging_publish(staging, &error) == MH_ERROR_STATE);
	CHECK(rename_count == 3);
	mh_filesystem_staging_close(staging);
	CHECK(GetFileAttributesW(L"Game") == INVALID_FILE_ATTRIBUTES);
	check_text(L"Game.openmotorex-backup/value", "recover me");
	check_text(L"Game.openmotorex-backup/userdata/save", "saved");
	blocked = NULL;
	CHECK(mh_filesystem_staging_update("Game", &blocked, &error) ==
		MH_ERROR_STATE);
	CHECK(blocked == NULL);
	CHECK(MoveFileW(L"Game.openmotorex-backup", L"Game"));
	check_text(L"Game/value", "recover me");
	fail_from = fail_through = 0;
}

static void
check_interruption(void)
{
	WCHAR binary[MAX_PATH], command[MAX_PATH + 40];
	STARTUPINFOW startup;
	PROCESS_INFORMATION process;
	WIN32_FIND_DATAW entry;
	HANDLE scan;
	DWORD code, length;

	length = GetModuleFileNameW(NULL, binary, MAX_PATH);
	CHECK(length != 0 && length < MAX_PATH);
	wcscpy(command, L"\"");
	wcscat(command, binary);
	wcscat(command, L"\" --interrupt");
	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	CHECK(CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL,
		&startup, &process));
	CHECK(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
	CHECK(GetExitCodeProcess(process.hProcess, &code) && code == 17);
	CHECK(CloseHandle(process.hThread));
	CHECK(CloseHandle(process.hProcess));
	CHECK(GetFileAttributesW(L"Game") == INVALID_FILE_ATTRIBUTES);
	check_text(L"Game.openmotorex-backup/value", "recover me");
	CHECK(MoveFileW(L"Game.openmotorex-backup", L"Game"));

	/*
	 * The killed child leaves a complete private tree; remove this fixture.
	 */
	scan = FindFirstFileW(L"openmotorex_tmp_*", &entry);
	CHECK(scan != INVALID_HANDLE_VALUE);
	wcscpy(command, entry.cFileName);
	wcscat(command, L"/value");
	CHECK(DeleteFileW(command));
	wcscpy(command, entry.cFileName);
	wcscat(command, L"/userdata/save");
	CHECK(DeleteFileW(command));
	wcscpy(command, entry.cFileName);
	wcscat(command, L"/userdata");
	CHECK(RemoveDirectoryW(command));
	CHECK(RemoveDirectoryW(entry.cFileName));
	CHECK(!FindNextFileW(scan, &entry));
	CHECK(FindClose(scan));
}

int
main(int argc, char *argv[])
{
	WCHAR temp[MAX_PATH], base[MAX_PATH], previous[MAX_PATH];
	DWORD length;
	struct mh_filesystem_staging *staging;

	if (argc == 2 && strcmp(argv[1], "--interrupt") == 0) {
		staging = prepare_update();
		crash_after_backup = 1;
		mh_filesystem_staging_publish(staging, &error);
		return 1;
	}

	length = GetCurrentDirectoryW(MAX_PATH, previous);
	CHECK(length != 0 && length < MAX_PATH);
	CHECK(GetTempPathW(MAX_PATH, temp) != 0);
	CHECK(GetTempFileNameW(temp, L"omh", 0, base) != 0);
	CHECK(DeleteFileW(base));
	CHECK(CreateDirectoryW(base, NULL));
	CHECK(SetCurrentDirectoryW(base));
	check_base();
	check_scratch();
	check_update();
	check_recovery();
	check_interruption();
	CHECK(DeleteFileW(L"Game/value"));
	CHECK(DeleteFileW(L"Game/userdata/save"));
	CHECK(RemoveDirectoryW(L"Game/userdata"));
	CHECK(RemoveDirectoryW(L"Game"));
	CHECK(SetCurrentDirectoryW(previous));
	CHECK(RemoveDirectoryW(base));

	return 0;
}
