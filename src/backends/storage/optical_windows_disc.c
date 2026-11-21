#define WIN32_LEAN_AND_MEAN
/* The CD driver declarations require the Win32 and storage types first. */
/* clang-format off */
#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
/* clang-format on */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/optical_windows_provider.h"
#include "interfaces/storage/optical_backend.h"

enum {
	READ_ATTEMPTS = 3,
	TOC_HEADER_BYTES = 4,
	TOC_TRACK_BYTES = 8
};

struct windows_disc {
	HANDLE			handle;
	void			*buffer;
	int			locked;
	DWORD			media_change;
	unsigned long		starts[MH_OPTICAL_TRACK_LIMIT];
	struct mh_optical_track	tracks[MH_OPTICAL_TRACK_LIMIT];
	size_t			count;
};

static int
separator(char value)
{
	return value == '/' || value == '\\';
}

static WCHAR
drive_letter(const char *path)
{
	size_t length;
	char letter;

	length = strlen(path);

	/* The Windows command-line host normalizes backslashes to slashes. */
	if (length >= 4 && separator(path[0]) && separator(path[1]) &&
		path[2] == '.' && separator(path[3])) {
		path += 4;
		length -= 4;
	}

	if ((length != 2 && length != 3) || path[1] != ':' ||
		(length == 3 && !separator(path[2])))
		return 0;

	letter = path[0];

	if (letter >= 'a' && letter <= 'z')
		letter = (char)(letter - 'a' + 'A');

	return letter >= 'A' && letter <= 'Z' ? (WCHAR)letter : 0;
}

enum mh_error_code
mh_windows_optical_probe(
	const char *path, int *matches, struct mh_error *error_out)
{
	WCHAR root[] = L"D:\\";

	if (path == NULL || matches == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid CD drive probe arguments");

	root[0] = drive_letter(path);
	*matches = root[0] != 0 && GetDriveTypeW(root) == DRIVE_CDROM;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
verify_media(struct windows_disc *disc, int initial, struct mh_error *error_out)
{
	DWORD change, returned;

	change = 0;
	returned = 0;

	if (!DeviceIoControl(disc->handle, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0,
		&change, sizeof(change), &returned, NULL) ||
		returned != sizeof(change))
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot verify CD media; insert a readable game CD and retry");

	if (!initial && change != disc->media_change)
		return mh_error_set(error_out, MH_ERROR_IO,
			"CD media changed during extraction; retry with the original disc");

	disc->media_change = change;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_toc(struct windows_disc *disc, struct mh_error *error_out)
{
	CDROM_READ_TOC_EX request;
	unsigned char bytes[TOC_HEADER_BYTES +
		(MH_OPTICAL_TRACK_LIMIT + 1) * TOC_TRACK_BYTES];
	const unsigned char *entry;
	DWORD returned;
	unsigned long start;
	unsigned int number, control, length;
	size_t i;

	memset(&request, 0, sizeof(request));
	request.Format = CDROM_READ_TOC_EX_FORMAT_TOC;
	returned = 0;

	if (!DeviceIoControl(disc->handle, IOCTL_CDROM_READ_TOC_EX, &request,
		sizeof(request), bytes, sizeof(bytes), &returned, NULL))
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot read CD table of contents; insert a readable game CD");

	if (returned < TOC_HEADER_BYTES || returned > sizeof(bytes))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated CD table of contents");

	length = ((unsigned int)bytes[0] << 8) + bytes[1] + 2;

	if (bytes[2] < 1 || bytes[3] < bytes[2] ||
		bytes[3] > MH_OPTICAL_TRACK_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid CD track count");

	disc->count = (size_t)(bytes[3] - bytes[2]) + 1;

	if (length > returned ||
		length < TOC_HEADER_BYTES + (disc->count + 1) * TOC_TRACK_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete CD track boundaries");

	for (i = 0; i <= disc->count; i++) {
		entry = bytes + TOC_HEADER_BYTES + i * TOC_TRACK_BYTES;
		number = i == disc->count ? 0xaa : bytes[2] + (unsigned int)i;
		control = entry[1] & 0x0f;
		start = ((unsigned long)entry[4] << 24) |
			((unsigned long)entry[5] << 16) |
			((unsigned long)entry[6] << 8) | entry[7];

		if (entry[2] != number || start > LONG_MAX ||
			(i != 0 && start <= disc->starts[i - 1]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid CD track boundaries");

		if (i != 0)
			disc->tracks[i - 1].sectors =
				start - disc->starts[i - 1];

		if (i == disc->count)
			break;

		if ((control & AUDIO_DATA_TRACK) == 0 &&
			(control &
				(AUDIO_WITH_PREEMPHASIS |
					TWO_FOUR_CHANNEL_AUDIO)) != 0)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"CD audio requires unsupported emphasis or channel processing");

		disc->starts[i] = start;
		disc->tracks[i].number = number;
		disc->tracks[i].kind = (control & AUDIO_DATA_TRACK)
			? MH_OPTICAL_DATA
			: MH_OPTICAL_AUDIO;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static DWORD
read_blocks(struct windows_disc *disc, enum mh_optical_track_kind kind,
	unsigned long sector, unsigned int count, DWORD size)
{
	RAW_READ_INFO request;
	LARGE_INTEGER offset;
	DWORD returned;
	BOOL success;

	/* Both APIs address sectors in 2048-byte units, including CDDA. */
	offset.QuadPart = (LONGLONG)sector * MH_OPTICAL_DATA_BYTES;
	returned = 0;

	if (kind == MH_OPTICAL_AUDIO) {
		memset(&request, 0, sizeof(request));
		request.DiskOffset = offset;
		request.SectorCount = count;
		request.TrackMode = CDDA;
		success = DeviceIoControl(disc->handle, IOCTL_CDROM_RAW_READ,
			&request, sizeof(request), disc->buffer, size,
			&returned, NULL);
	} else {
		if (!SetFilePointerEx(disc->handle, offset, NULL, FILE_BEGIN))
			return GetLastError();

		success = ReadFile(
			disc->handle, disc->buffer, size, &returned, NULL);
	}

	if (!success)
		return GetLastError();

	return returned == size ? ERROR_SUCCESS : ERROR_READ_FAULT;
}

static enum mh_error_code
read_sectors(void *context, size_t index, unsigned long sector,
	unsigned int count, void *output, struct mh_error *error_out)
{
	struct windows_disc *disc;
	DWORD failure, size;
	unsigned int attempt;
	enum mh_error_code status;

	disc = context;
	sector += disc->starts[index];
	size = count *
		(disc->tracks[index].kind == MH_OPTICAL_DATA
				? MH_OPTICAL_DATA_BYTES
				: MH_OPTICAL_AUDIO_BYTES);

	for (attempt = 0; attempt < READ_ATTEMPTS; attempt++) {
		status = verify_media(disc, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		failure = read_blocks(
			disc, disc->tracks[index].kind, sector, count, size);

		if (failure == ERROR_SUCCESS) {
			status = verify_media(disc, 0, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			memcpy(output, disc->buffer, size);

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (failure != ERROR_CRC && failure != ERROR_IO_DEVICE &&
			failure != ERROR_READ_FAULT &&
			failure != ERROR_SECTOR_NOT_FOUND)
			break;
	}

	return mh_error_set(error_out, MH_ERROR_IO,
		"cannot read CD sectors; check the disc and drive");
}

static void
close_disc(void *context)
{
	struct windows_disc *disc;
	PREVENT_MEDIA_REMOVAL removal;
	DWORD returned;

	disc = context;

	if (disc->locked) {
		removal.PreventMediaRemoval = FALSE;
		DeviceIoControl(disc->handle, IOCTL_STORAGE_EJECTION_CONTROL,
			&removal, sizeof(removal), NULL, 0, &returned, NULL);
	}

	if (disc->handle != INVALID_HANDLE_VALUE)
		CloseHandle(disc->handle);

	if (disc->buffer != NULL)
		VirtualFree(disc->buffer, 0, MEM_RELEASE);

	free(disc);
}

enum mh_error_code
mh_windows_optical_open(const char *path, struct mh_optical_disc **output,
	struct mh_error *error_out)
{
	static const struct mh_optical_ops ops = {read_sectors, close_disc};
	WCHAR name[] = L"\\\\.\\D:";
	struct windows_disc *disc;
	PREVENT_MEDIA_REMOVAL removal;
	DWORD returned;
	int matches;
	enum mh_error_code status;

	if (path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid CD drive open arguments");

	status = mh_windows_optical_probe(path, &matches, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!matches)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"source is not a CD drive root");

	disc = calloc(1, sizeof(*disc));

	if (disc == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening CD drive");

	name[4] = drive_letter(path);
	disc->handle = CreateFileW(name, GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_NO_BUFFERING, NULL);

	if (disc->handle == INVALID_HANDLE_VALUE) {
		status = mh_error_set(error_out, MH_ERROR_IO,
			GetLastError() == ERROR_ACCESS_DENIED
				? "permission denied opening CD drive; try an elevated terminal"
				: "cannot open CD drive; insert a readable game CD");
		close_disc(disc);
		return status;
	}

	/*
	 * Unbuffered reads must use aligned storage and never read ahead into
	 * audio.
	 */
	disc->buffer = VirtualAlloc(NULL,
		MH_OPTICAL_READ_SECTORS * MH_OPTICAL_AUDIO_BYTES,
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	if (disc->buffer == NULL) {
		close_disc(disc);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading CD drive");
	}

	/*
	 * Prevent accidental ejection when the drive supports it. A rejected
	 * tray lock must not prevent readable media from being extracted:
	 * mandatory change-counter checks still guard the TOC and every read.
	 * Successful per-handle locks are released on close or process exit.
	 */
	removal.PreventMediaRemoval = TRUE;
	disc->locked = DeviceIoControl(disc->handle,
			IOCTL_STORAGE_EJECTION_CONTROL, &removal,
			sizeof(removal), NULL, 0, &returned, NULL) != 0;
	status = verify_media(disc, 1, error_out);

	if (status == MH_ERROR_NONE)
		status = read_toc(disc, error_out);

	if (status == MH_ERROR_NONE)
		status = verify_media(disc, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_optical_disc_create(&ops, disc, disc->tracks,
			disc->count, output, error_out);

	if (status != MH_ERROR_NONE)
		close_disc(disc);

	return status;
}
