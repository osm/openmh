#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winioctl.h>

#include "backends/storage/filesystem_file_read.h"
#include "backends/storage/filesystem_platform.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/filesystem_windows_path.h"
#include "engine/storage/asset_path.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "Windows resource line %d: %s\n",      \
				__LINE__, #condition);                         \
			return 1;                                              \
		}                                                              \
	} while (0)

struct listing {
	unsigned int	count;
	int		stop, failed;
};

static enum mh_directory_action
visit(void *context, const char *name)
{
	struct listing *listing;

	listing = context;
	listing->count++;

	if (listing->failed)
		return MH_DIRECTORY_FAIL;

	if (strcmp(name, "r\303\244ce.dat") != 0)
		listing->failed = 1;

	return listing->stop ? MH_DIRECTORY_STOP : MH_DIRECTORY_CONTINUE;
}

static int
check_file(const char *root)
{
	const struct mh_resource_source *source;
	struct mh_file bytes;
	struct listing listing;
	struct mh_data_directory *directory;
	unsigned long size;
	FILE *stream;
	char path[1024], resolved[1024], sample[4];
	int found;

	source = mh_filesystem_resources();
	sprintf(path, "%s/r\303\244ce.dat", root);
	CHECK(mh_resource_source_read(source, path, &bytes, NULL) ==
		MH_ERROR_NONE);
	CHECK(bytes.size == 7 && memcmp(bytes.data, "fixture", 7) == 0);
	free(bytes.data);
	CHECK(mh_file_read(path, &bytes, NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == 7 && memcmp(bytes.data, "fixture", 7) == 0);
	free(bytes.data);
	CHECK(mh_resource_source_size(source, path, &size, NULL) ==
		MH_ERROR_NONE);
	CHECK(size == 7);
	CHECK(mh_resource_source_read_at(source, path, 2, sample, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(sample, "xtur", 4) == 0);
	CHECK(mh_filesystem_open_read(path, &stream, NULL) == MH_ERROR_NONE);
	CHECK(mh_filesystem_stream_seek(stream, 2, NULL) == MH_ERROR_NONE);
	CHECK(mh_filesystem_stream_size(stream, &size, NULL) == MH_ERROR_NONE);
	CHECK(fgetc(stream) == 'x');
	CHECK(fclose(stream) == 0);
	directory = NULL;
	CHECK(mh_resource_source_open_directory(
		source, path, &directory, NULL) == MH_ERROR_IO);
	CHECK(directory == NULL);

	memset(&listing, 0, sizeof(listing));
	CHECK(mh_resource_source_list(source, root, visit, &listing, NULL) ==
		MH_ERROR_NONE);
	CHECK(listing.count == 1 && !listing.failed);
	listing.stop = 1;
	CHECK(mh_resource_source_list(source, root, visit, &listing, NULL) ==
		MH_ERROR_NONE);
	CHECK(listing.count == 2);
	listing.failed = 1;
	CHECK(mh_resource_source_list(source, root, visit, &listing, NULL) ==
		MH_ERROR_IO);
	CHECK(mh_asset_path_find_from(source, root, "R\303\244CE.DAT", resolved,
		sizeof(resolved), &found, NULL) == MH_ERROR_NONE);
	CHECK(found && strcmp(path, resolved) == 0);

	bytes.data = (unsigned char *)sample;
	bytes.size = 19;
	CHECK(mh_resource_source_read_bounded(source, path, 6, &bytes, NULL) ==
		MH_ERROR_RANGE);
	CHECK(bytes.data == (unsigned char *)sample && bytes.size == 19);
	CHECK(mh_resource_source_read(source, root, &bytes, NULL) ==
		MH_ERROR_RANGE);
	CHECK(bytes.data == (unsigned char *)sample && bytes.size == 19);
	CHECK(mh_resource_source_read(source, "\xff", &bytes, NULL) ==
		MH_ERROR_INVALID);
	strcat(path, ".missing");
	CHECK(mh_resource_source_read(source, path, &bytes, NULL) ==
		MH_ERROR_NOT_FOUND);

	return 0;
}

static int
check_large_file(const WCHAR *root, const char *utf8)
{
	WCHAR path[1024];
	char name[1024], bytes[4];
	HANDLE file;
	LARGE_INTEGER offset;
	DWORD count;
	unsigned long size;
	const struct mh_resource_source *source;

	lstrcpyW(path, root);
	lstrcatW(path, L"\\large.dat");
	file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, NULL);
	CHECK(file != INVALID_HANDLE_VALUE);
	CHECK(DeviceIoControl(
		file, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &count, NULL));
	offset.LowPart = 0x80000010UL;
	offset.HighPart = 0;
	CHECK(SetFilePointerEx(file, offset, NULL, FILE_BEGIN));
	CHECK(WriteFile(file, "tail", 4, &count, NULL) && count == 4);
	CHECK(FlushFileBuffers(file));
	sprintf(name, "%s/large.dat", utf8);
	source = mh_filesystem_resources();
	CHECK(mh_resource_source_size(source, name, &size, NULL) ==
		MH_ERROR_NONE);
	CHECK(size == 0x80000014UL);
	CHECK(mh_resource_source_read_at(source, name, 0x80000010UL, bytes, 4,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(bytes, "tail", 4) == 0);
	CHECK(mh_resource_source_read_at(source, name, size, bytes, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_resource_source_read_at(source, name, size - 1, bytes, 4,
		NULL) == MH_ERROR_RANGE);
	CHECK(CloseHandle(file));

	return 0;
}

static int
check_directories(const char *root)
{
	WCHAR *wide;
	char path[1024], executable[4096], unchanged[4096];

	CHECK(mh_filesystem_executable_directory(
		executable, sizeof(executable), NULL) == MH_ERROR_NONE);
	CHECK(mh_windows_path_to_wide(root, &wide, NULL) == MH_ERROR_NONE);
	CHECK(SetCurrentDirectoryW(wide));
	free(wide);
	CHECK(mh_filesystem_executable_directory(
		unchanged, sizeof(unchanged), NULL) == MH_ERROR_NONE);
	CHECK(strcmp(executable, unchanged) == 0);
	strcpy(unchanged, "retained");
	CHECK(mh_filesystem_executable_directory(unchanged, 1, NULL) ==
		MH_ERROR_RANGE);
	CHECK(strcmp(unchanged, "retained") == 0);
	sprintf(path, "%s/userdata/Records", root);
	CHECK(mh_filesystem_create_directory(NULL, path, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_filesystem_create_directory(NULL, path, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_filesystem_check_directory(NULL, path, NULL) == MH_ERROR_NONE);
	CHECK(mh_windows_path_to_wide(path, &wide, NULL) == MH_ERROR_NONE);
	CHECK(RemoveDirectoryW(wide));
	free(wide);
	sprintf(path, "%s/userdata", root);
	CHECK(mh_windows_path_to_wide(path, &wide, NULL) == MH_ERROR_NONE);
	CHECK(RemoveDirectoryW(wide));
	free(wide);
	CHECK(mh_filesystem_check_directory(NULL, path, NULL) ==
		MH_ERROR_NOT_FOUND);

	return 0;
}

int
main(void)
{
	WCHAR temp[MAX_PATH], base[MAX_PATH], root[1024], path[1024];
	WCHAR previous[4096];
	char *utf8;
	FILE *file;
	int result;

	CHECK(GetCurrentDirectoryW(
		sizeof(previous) / sizeof(*previous), previous) != 0);
	CHECK(GetTempPathW(MAX_PATH, temp) != 0);
	CHECK(GetTempFileNameW(temp, L"omh", 0, base) != 0);
	CHECK(DeleteFileW(base));
	CHECK(CreateDirectoryW(base, NULL));
	lstrcpyW(root, base);
	lstrcatW(root, L"\\OpenMH \x00e5\x00e4\x00f6");
	CHECK(CreateDirectoryW(root, NULL));
	CHECK(mh_windows_path_to_utf8(root, &utf8, NULL) == MH_ERROR_NONE);
	CHECK(strlen(utf8) < 900);
	CHECK(strstr(utf8, "OpenMH \303\245\303\244\303\266") != NULL);
	lstrcpyW(path, root);
	lstrcatW(path,
		L"\\r\x00e4"
		L"ce.dat");
	file = _wfopen(path, L"wb");
	CHECK(file != NULL);
	CHECK(fwrite("fixture", 1, 7, file) == 7 && fclose(file) == 0);

	result = check_file(utf8);

	if (result == 0)
		result = check_large_file(root, utf8);

	if (result == 0)
		result = check_directories(utf8);

	free(utf8);
	CHECK(SetCurrentDirectoryW(previous));
	CHECK(DeleteFileW(path));
	CHECK(RemoveDirectoryW(root));
	CHECK(RemoveDirectoryW(base));

	if (result == 0)
		puts("Windows resources: Unicode, enumeration, executable root and >2GB reads passed");

	return result;
}
