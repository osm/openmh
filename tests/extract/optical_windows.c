#define WIN32_LEAN_AND_MEAN
/* The CD driver declarations require the Win32 and storage types first. */
/* clang-format off */
#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
/* clang-format on */

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "backends/storage/optical_windows_provider.h"

enum {
	TRACK_SECTORS = 64
};

enum toc_fault {
	TOC_VALID,
	TOC_SHORT_HEADER,
	TOC_SHORT_TRACKS,
	TOC_BAD_LENGTH,
	TOC_BAD_COUNT,
	TOC_BAD_NUMBER,
	TOC_NEGATIVE_START,
	TOC_UNORDERED,
	TOC_EMPHASIS,
	TOC_FOUR_CHANNEL,
	TOC_BAD_LEADOUT
};

static struct {
	UINT		drive_type;
	DWORD		open_error, read_error, media_change, lock_error;
	enum toc_fault	toc_fault;
	int		verify_error, toc_error, short_verify;
	int		failed_reads, short_read, change_during_read;
	unsigned int	opens, closes, locks, unlocks, reads;
	unsigned long	sector;
} device;

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static HANDLE
device_handle(void)
{
	return (HANDLE)(ULONG_PTR)71;
}

static void
reset_device(void)
{
	memset(&device, 0, sizeof(device));
	device.drive_type = DRIVE_CDROM;
}

static UINT WINAPI
drive_type(LPCWSTR root)
{
	check(wcscmp(root, L"D:\\") == 0, "probe uses an absolute drive root");

	return device.drive_type;
}

static HANDLE WINAPI
open_drive(LPCWSTR name, DWORD access, DWORD sharing,
	LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags,
	HANDLE template_file)
{
	check(wcscmp(name, L"\\\\.\\D:") == 0,
		"open uses a volume device without trailing slash");

	check(access == GENERIC_READ &&
			sharing == (FILE_SHARE_READ | FILE_SHARE_WRITE),
		"CD opens read-only with shared access");
	check(security == NULL && creation == OPEN_EXISTING &&
			flags == FILE_FLAG_NO_BUFFERING &&
			template_file == NULL,
		"unbuffered synchronous CD open");
	device.opens++;

	if (device.open_error != 0) {
		SetLastError(device.open_error);
		return INVALID_HANDLE_VALUE;
	}

	return device_handle();
}

static BOOL WINAPI
close_drive(HANDLE handle)
{
	check(handle == device_handle(), "close owns the CD handle");
	device.closes++;

	return TRUE;
}

static BOOL
transfer(void *output, DWORD size, DWORD *returned, unsigned int stride)
{
	unsigned char *bytes;
	unsigned int i;

	device.reads++;
	check(size != 0 && size % stride == 0 &&
			size / stride <= MH_OPTICAL_READ_SECTORS &&
			device.sector % TRACK_SECTORS + size / stride <=
				TRACK_SECTORS,
		"bounded transfers never cross a track boundary");
	check((ULONG_PTR)output % 4096 == 0,
		"unbuffered transfer uses aligned memory");

	if (device.failed_reads > 0) {
		device.failed_reads--;
		SetLastError(device.read_error);
		return FALSE;
	}

	bytes = output;

	for (i = 0; i < size; i++)
		bytes[i] = (unsigned char)((device.sector * stride + i) % 251);

	*returned = device.short_read ? size - 1 : size;

	if (device.change_during_read)
		device.media_change++;

	return TRUE;
}

static BOOL WINAPI
seek_drive(HANDLE handle, LARGE_INTEGER offset, PLARGE_INTEGER result,
	DWORD method)
{
	check(handle == device_handle() && result == NULL &&
			method == FILE_BEGIN,
		"data read seeks on the CD handle");

	check(offset.QuadPart >= 0 &&
			offset.QuadPart % MH_OPTICAL_DATA_BYTES == 0,
		"data read seeks to a sector boundary");
	device.sector =
		(unsigned long)(offset.QuadPart / MH_OPTICAL_DATA_BYTES);

	return TRUE;
}

static BOOL WINAPI
read_drive(HANDLE handle, LPVOID output, DWORD size, LPDWORD returned,
	LPOVERLAPPED overlapped)
{
	check(handle == device_handle() && overlapped == NULL &&
			device.sector < TRACK_SECTORS,
		"ReadFile reads cooked data sectors synchronously");

	return transfer(output, size, returned, MH_OPTICAL_DATA_BYTES);
}

static void
write_toc(unsigned char *bytes, DWORD *returned)
{
	unsigned int i;

	memset(bytes, 0, 28);
	bytes[1] = 26;
	bytes[2] = 1;
	bytes[3] = 2;

	for (i = 0; i < 3; i++) {
		bytes[4 + i * 8 + 1] = i == 0 ? 0x14 : 0x10;
		bytes[4 + i * 8 + 2] = i == 2 ? 0xaa : (unsigned char)(i + 1);
		bytes[4 + i * 8 + 7] = (unsigned char)(i * TRACK_SECTORS);
	}

	*returned = 28;

	switch (device.toc_fault) {
	case TOC_VALID:
		break;
	case TOC_SHORT_HEADER:
		*returned = 3;
		break;

	case TOC_SHORT_TRACKS:
		*returned = 27;
		break;

	case TOC_BAD_LENGTH:
		bytes[1] = 4;
		break;

	case TOC_BAD_COUNT:
		bytes[3] = 100;
		break;

	case TOC_BAD_NUMBER:
		bytes[14] = 3;
		break;

	case TOC_NEGATIVE_START:
		bytes[8] = 0xff;
		break;

	case TOC_UNORDERED:
		bytes[19] = 0;
		break;

	case TOC_EMPHASIS:
		bytes[13] |= 1;
		break;

	case TOC_FOUR_CHANNEL:
		bytes[13] |= 8;
		break;

	case TOC_BAD_LEADOUT:
		bytes[22] = 3;
		break;
	}
}

static BOOL WINAPI
control_drive(HANDLE handle, DWORD operation, LPVOID input, DWORD input_size,
	LPVOID output, DWORD output_size, LPDWORD returned,
	LPOVERLAPPED overlapped)
{
	const PREVENT_MEDIA_REMOVAL *removal;
	const CDROM_READ_TOC_EX *toc;
	const RAW_READ_INFO *raw;

	check(handle == device_handle() && overlapped == NULL,
		"control request uses the synchronous CD handle");
	*returned = 0;

	if (operation == IOCTL_STORAGE_EJECTION_CONTROL) {
		removal = input;
		check(input_size == sizeof(*removal) && output == NULL &&
				output_size == 0,
			"media lock request has no output buffer");

		if (removal->PreventMediaRemoval) {
			if (device.lock_error) {
				SetLastError(device.lock_error);
				return FALSE;
			}

			device.locks++;
		} else
			device.unlocks++;

		return TRUE;
	}

	if (operation == IOCTL_STORAGE_CHECK_VERIFY) {
		check(input == NULL && input_size == 0 &&
				output_size == sizeof(DWORD),
			"verify obtains the media change counter");

		if (device.verify_error) {
			SetLastError(ERROR_NOT_READY);
			return FALSE;
		}

		*(DWORD *)output = device.media_change;
		*returned = device.short_verify ? 0 : sizeof(DWORD);

		return TRUE;
	}

	if (operation == IOCTL_CDROM_READ_TOC_EX) {
		toc = input;
		check(input_size == sizeof(*toc) &&
				toc->Format == CDROM_READ_TOC_EX_FORMAT_TOC &&
				toc->Msf == 0 && toc->SessionTrack == 0 &&
				output_size >= 28,
			"TOC requests logical addresses for all tracks");

		if (device.toc_error) {
			SetLastError(ERROR_NOT_READY);
			return FALSE;
		}

		write_toc(output, returned);

		return TRUE;
	}

	if (operation == IOCTL_CDROM_RAW_READ) {
		raw = input;
		check(input_size == sizeof(*raw) && raw->TrackMode == CDDA &&
				raw->SectorCount * MH_OPTICAL_AUDIO_BYTES ==
					output_size &&
				raw->DiskOffset.QuadPart %
						MH_OPTICAL_DATA_BYTES ==
					0,
			"raw audio addresses 2048-byte sectors but returns 2352-byte PCM payloads");
		device.sector = (unsigned long)(raw->DiskOffset.QuadPart /
			MH_OPTICAL_DATA_BYTES);
		check(device.sector >= TRACK_SECTORS &&
				device.sector < 2 * TRACK_SECTORS,
			"audio read uses the absolute audio track start");
		return transfer(
			output, output_size, returned, MH_OPTICAL_AUDIO_BYTES);
	}

	check(0, "unexpected CD control request");
	SetLastError(ERROR_INVALID_FUNCTION);

	return FALSE;
}

UINT(WINAPI *__wrap___imp_GetDriveTypeW)(LPCWSTR) = drive_type;
HANDLE(WINAPI *__wrap___imp_CreateFileW)(LPCWSTR, DWORD, DWORD,
	LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) = open_drive;
BOOL(WINAPI *__wrap___imp_CloseHandle)(HANDLE) = close_drive;
BOOL(WINAPI *__wrap___imp_SetFilePointerEx)(
	HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD) = seek_drive;
BOOL(WINAPI *__wrap___imp_ReadFile)(
	HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) = read_drive;
BOOL(WINAPI *__wrap___imp_DeviceIoControl)(HANDLE, DWORD, LPVOID, DWORD, LPVOID,
	DWORD, LPDWORD, LPOVERLAPPED) = control_drive;

static void
check_probes(void)
{
	static const char *const roots[] = {
		"D:", "d:/", "D:\\", "\\\\.\\d:", "//./D:"};

	static const char *const files[] = {"", "D", "D:disc", "D:/disc.iso",
		"D://", "//server/disc", "1:"};

	int matches;
	size_t i;

	reset_device();

	for (i = 0; i < sizeof(roots) / sizeof(roots[0]); i++)
		check(mh_windows_optical_probe(roots[i], &matches, NULL) ==
					MH_ERROR_NONE &&
				matches,
			"CD drive root is recognized");

	for (i = 0; i < sizeof(files) / sizeof(files[0]); i++)
		check(mh_windows_optical_probe(files[i], &matches, NULL) ==
					MH_ERROR_NONE &&
				!matches,
			"file and directory sources retain normal handling");

	device.drive_type = DRIVE_FIXED;
	check(mh_windows_optical_probe("D:", &matches, NULL) == MH_ERROR_NONE &&
			!matches,
		"fixed disks are not optical sources");
	matches = 71;
	check(mh_windows_optical_probe(NULL, &matches, NULL) ==
				MH_ERROR_INVALID &&
			matches == 71,
		"invalid probe preserves output");
}

static void
check_open_failures(void)
{
	struct mh_optical_disc *disc;
	int fault;

	for (fault = TOC_SHORT_HEADER; fault <= TOC_BAD_LEADOUT; fault++) {
		reset_device();
		device.toc_fault = (enum toc_fault)fault;
		disc = NULL;
		check(mh_windows_optical_open("D:", &disc, NULL) !=
					MH_ERROR_NONE &&
				disc == NULL && device.closes == 1 &&
				device.unlocks == 1,
			"invalid TOC preserves output and releases the locked drive");
	}

	for (fault = 0; fault < 6; fault++) {
		reset_device();
		device.open_error = fault == 0 ? ERROR_ACCESS_DENIED : 0;
		device.lock_error = fault == 1 ? ERROR_INVALID_FUNCTION : 0;
		device.verify_error = fault == 1 || fault == 2;
		device.toc_error = fault == 3;
		device.short_verify = fault == 4;
		device.drive_type = fault == 5 ? DRIVE_FIXED : DRIVE_CDROM;
		disc = NULL;
		check(mh_windows_optical_open("D:", &disc, NULL) !=
					MH_ERROR_NONE &&
				disc == NULL,
			"failed open preserves output");
		check(device.closes ==
					(unsigned int)(fault != 0 &&
						fault != 5) &&
				device.unlocks ==
					(unsigned int)(fault >= 2 &&
						fault <= 4),
			"open failure releases only acquired handles and media locks");
	}
}

static void
check_reads(DWORD lock_error)
{
	struct mh_optical_disc *disc;
	const struct mh_resource_source *source;
	unsigned char bytes[4096];
	size_t i;

	reset_device();
	device.lock_error = lock_error;
	disc = NULL;
	check(mh_windows_optical_open("//./d:", &disc, NULL) == MH_ERROR_NONE,
		"open mixed-mode CD");

	if (disc == NULL)
		return;

	source = mh_optical_disc_resources(disc);
	check(mh_optical_disc_count(disc) == 2, "TOC exposes both tracks");
	device.failed_reads = 2;
	device.read_error = ERROR_CRC;
	check(mh_resource_source_read_at(source, "track01", 62UL * 2048, bytes,
		sizeof(bytes), NULL) == MH_ERROR_NONE &&
			device.reads == 3,
		"retry transient errors at the data/audio boundary");

	for (i = 0; i < sizeof(bytes); i++)
		check(bytes[i] == (unsigned char)((62UL * 2048 + i) % 251),
			"cooked data payload is unchanged");

	check(mh_resource_source_read_at(source, "track02", 1, bytes,
		sizeof(bytes), NULL) == MH_ERROR_NONE,
		"read unaligned audio range");

	for (i = 0; i < sizeof(bytes); i++)
		check(bytes[i] ==
				(unsigned char)((TRACK_SECTORS * 2352UL + 1 +
							i) %
					251),
			"audio preserves byte order and range offsets");

	check(mh_resource_source_read_at(source, "track02", 64UL * 2352 - 1,
		bytes, 1, NULL) == MH_ERROR_NONE,
		"read final audio sector without crossing leadout");
	mh_optical_disc_close(disc);
	check(device.locks == (unsigned int)(lock_error == 0) &&
			device.unlocks == device.locks && device.closes == 1,
		"successful extraction releases only acquired locks and handles");
}

static void
check_transfer_failures(DWORD lock_error)
{
	struct mh_optical_disc *disc;
	unsigned char bytes[2];
	unsigned int fault, audio, attempts;

	for (audio = 0; audio < 2; audio++) {
		for (fault = 0; fault < 6; fault++) {
			reset_device();
			device.lock_error = lock_error;
			disc = NULL;
			check(mh_windows_optical_open("D:", &disc, NULL) ==
					MH_ERROR_NONE,
				"open transfer fault fixture");

			if (disc == NULL)
				continue;

			device.failed_reads = fault < 2 ? 10 : 0;
			device.read_error =
				fault == 0 ? ERROR_IO_DEVICE : ERROR_NOT_READY;
			device.short_read = fault == 2;
			device.media_change = fault == 3 ? 1 : 0;
			device.change_during_read = fault == 4;
			device.verify_error = fault == 5;
			attempts = fault == 0 || fault == 2
				? 3
				: (fault == 3 || fault == 5 ? 0 : 1);
			memset(bytes, 0xff, sizeof(bytes));
			check(mh_resource_source_read_at(
				mh_optical_disc_resources(disc),
				audio ? "track02" : "track01", 0, bytes,
				sizeof(bytes), NULL) == MH_ERROR_IO &&
					device.reads == attempts &&
					bytes[0] == 0xff && bytes[1] == 0xff,
				"failed/short reads and media changes never publish partial sectors");
			mh_optical_disc_close(disc);
			check(device.closes == 1 &&
					device.unlocks ==
						(unsigned int)(lock_error == 0),
				"read failures release the drive");
		}
	}
}

int
main(void)
{
	check_probes();
	check_open_failures();
	check_reads(ERROR_SUCCESS);
	check_reads(ERROR_INVALID_FUNCTION);
	check_reads(ERROR_NOT_SUPPORTED);
	check_reads(ERROR_ACCESS_DENIED);
	check_reads(ERROR_IO_DEVICE);
	check_transfer_failures(ERROR_SUCCESS);
	check_transfer_failures(ERROR_INVALID_FUNCTION);

	return failures != 0;
}
