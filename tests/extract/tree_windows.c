#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "backends/storage/filesystem_tree.h"

#define FIXTURE_SIZE 131071
#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr,                                        \
				"Windows tree line %d: %s (%s; Win32 %lu)\n",  \
				__LINE__, #condition,                          \
				error.message == NULL ? "no diagnostic"        \
						      : error.message,         \
				(unsigned long)GetLastError());                \
			exit(1);                                               \
		}                                                              \
	} while (0)

static struct mh_error error;
static unsigned char bytes[FIXTURE_SIZE];

static void
write_file(const WCHAR *path)
{
	HANDLE file;
	DWORD written;

	file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL, NULL);
	CHECK(file != INVALID_HANDLE_VALUE);
	CHECK(WriteFile(file, bytes, sizeof(bytes), &written, NULL));
	CHECK(written == sizeof(bytes));
	CHECK(CloseHandle(file));
}

static void
check_file(const WCHAR *path)
{
	unsigned char actual[4096];
	HANDLE file;
	DWORD count;
	size_t offset;

	file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	CHECK(file != INVALID_HANDLE_VALUE);
	offset = 0;

	for (;;) {
		CHECK(ReadFile(file, actual, sizeof(actual), &count, NULL));

		if (count == 0)
			break;

		CHECK(count <= sizeof(bytes) - offset);
		CHECK(memcmp(bytes + offset, actual, count) == 0);
		offset += count;
	}

	CHECK(offset == sizeof(bytes));
	CHECK(CloseHandle(file));
}

static enum mh_error_code
cancel_copy(void *context, const char *name, struct mh_error *error_out)
{
	unsigned int *calls;

	calls = context;
	(*calls)++;
	CHECK(strcmp(name, "data.bin") == 0);

	return mh_error_set(error_out, MH_ERROR_FAILED, "fixture cancellation");
}

static void
check_copy(void)
{
	DWORD attributes;

	CHECK(CreateDirectoryW(L"Source \x00e5", NULL));
	CHECK(CreateDirectoryW(L"Source \x00e5/Sub", NULL));
	CHECK(CreateDirectoryW(L"Source \x00e5/Empty", NULL));
	CHECK(CreateDirectoryW(L"Output", NULL));
	write_file(L"Source \x00e5/Sub/Asset \x00f6.dat");
	CHECK(SetFileAttributesW(L"Source \x00e5/Sub/Asset \x00f6.dat",
		FILE_ATTRIBUTE_READONLY));
	CHECK(mh_filesystem_copy_tree("Source \303\245", "Output", NULL, NULL,
		&error) == MH_ERROR_NONE);
	check_file(L"Output/Sub/Asset \x00f6.dat");
	attributes = GetFileAttributesW(L"Output/Empty");
	CHECK(attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_DIRECTORY));

	/*
	 * No merge, overwrite, or recursive copy into the source is permitted.
	 */
	CHECK(mh_filesystem_copy_tree("Source \303\245", "Output", NULL, NULL,
		&error) == MH_ERROR_EXISTS);
	check_file(L"Output/Sub/Asset \x00f6.dat");
	CHECK(mh_filesystem_copy_tree("Source \303\245", "Source \303\245/Sub",
		NULL, NULL, &error) == MH_ERROR_INVALID);
	CHECK(mh_filesystem_copy_tree("Output", "output", NULL, NULL, &error) ==
		MH_ERROR_INVALID);

	CHECK(SetFileAttributesW(
		L"Source \x00e5/Sub/Asset \x00f6.dat", FILE_ATTRIBUTE_NORMAL));
	CHECK(DeleteFileW(L"Source \x00e5/Sub/Asset \x00f6.dat"));
	CHECK(DeleteFileW(L"Output/Sub/Asset \x00f6.dat"));
	CHECK(RemoveDirectoryW(L"Source \x00e5/Sub"));
	CHECK(RemoveDirectoryW(L"Source \x00e5/Empty"));
	CHECK(RemoveDirectoryW(L"Source \x00e5"));
	CHECK(RemoveDirectoryW(L"Output/Sub"));
	CHECK(RemoveDirectoryW(L"Output/Empty"));
	CHECK(RemoveDirectoryW(L"Output"));
}

static void
check_failures(void)
{
	HANDLE writer;
	unsigned int calls;

	CHECK(CreateDirectoryW(L"Input", NULL));
	CHECK(CreateDirectoryW(L"Cancelled", NULL));
	CHECK(CreateDirectoryW(L"Busy", NULL));
	write_file(L"Input/data.bin");
	calls = 0;
	CHECK(mh_filesystem_copy_tree("Input", "Cancelled", cancel_copy, &calls,
		&error) == MH_ERROR_FAILED);
	CHECK(calls == 1 && strcmp(error.message, "fixture cancellation") == 0);
	CHECK(GetFileAttributesW(L"Cancelled/data.bin") ==
		INVALID_FILE_ATTRIBUTES);
	check_file(L"Input/data.bin");

	writer = CreateFileW(L"Input/data.bin", GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
		NULL);
	CHECK(writer != INVALID_HANDLE_VALUE);
	CHECK(mh_filesystem_copy_tree("Input", "Busy", NULL, NULL, &error) !=
		MH_ERROR_NONE);
	CHECK(CloseHandle(writer));
	CHECK(GetFileAttributesW(L"Busy/data.bin") == INVALID_FILE_ATTRIBUTES);
	check_file(L"Input/data.bin");

	CHECK(DeleteFileW(L"Input/data.bin"));
	CHECK(RemoveDirectoryW(L"Input"));
	CHECK(RemoveDirectoryW(L"Cancelled"));
	CHECK(RemoveDirectoryW(L"Busy"));
}

int
main(void)
{
	WCHAR temp[MAX_PATH], base[MAX_PATH], previous[MAX_PATH];
	DWORD length;
	size_t i;

	for (i = 0; i < sizeof(bytes); i++)
		bytes[i] = (unsigned char)(i * 37 + 11);

	length = GetCurrentDirectoryW(MAX_PATH, previous);
	CHECK(length != 0 && length < MAX_PATH);
	CHECK(GetTempPathW(MAX_PATH, temp) != 0);
	CHECK(GetTempFileNameW(temp, L"omh", 0, base) != 0);
	CHECK(DeleteFileW(base));
	CHECK(CreateDirectoryW(base, NULL));
	CHECK(SetCurrentDirectoryW(base));
	check_copy();
	check_failures();
	CHECK(SetCurrentDirectoryW(previous));
	CHECK(RemoveDirectoryW(base));

	return 0;
}
