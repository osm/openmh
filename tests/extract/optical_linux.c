#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>

#include <linux/cdrom.h>

#include <errno.h>
#include <fcntl.h>
#include <scsi/sg.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "backends/storage/optical_linux_provider.h"

enum {
	DEVICE_FD = 71,
	TRACK_SECTORS = 64
};

enum toc_fault {
	TOC_VALID,
	TOC_BAD_COUNT,
	TOC_NEGATIVE_START,
	TOC_UNORDERED,
	TOC_EMPHASIS
};

static struct {
	int		open_error, regular_file, invalid_toc;
	int		read_error, failed_reads, residual, scsi_error;
	unsigned int	reads, closes;
} device;

static int failures;

struct open_failure_case {
	enum toc_fault	fault;
	const char	*reason;
};

struct transfer_failure_case {
	int		error, failed_reads, residual, scsi_error;
	unsigned int	attempts;
	const char	*reason;
};

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

int
__wrap_open(const char *path, int flags, ...)
{
	(void)path;
	check((flags & O_ACCMODE) == O_RDONLY, "CD device opens read-only");

	if (device.open_error) {
		errno = device.open_error;
		return -1;
	}

	return DEVICE_FD;
}

int
__wrap_close(int fd)
{
	check(fd == DEVICE_FD, "close owns the correct device");
	device.closes++;

	return 0;
}

int
__wrap_stat(const char *path, struct stat *info)
{
	(void)path;
	memset(info, 0, sizeof(*info));
	info->st_mode = device.regular_file ? S_IFREG : S_IFBLK;

	return 0;
}

int
__wrap_fstat(int fd, struct stat *info)
{
	check(fd == DEVICE_FD, "inspect the opened device");

	return __wrap_stat(NULL, info);
}

static int
transfer(sg_io_hdr_t *request)
{
	unsigned char *command, *bytes;
	unsigned long sector;
	unsigned int count, stride, i;

	device.reads++;
	command = request->cmdp;
	sector = ((unsigned long)command[2] << 24) |
		((unsigned long)command[3] << 16) |
		((unsigned long)command[4] << 8) | command[5];
	count = command[8];
	stride = sector < TRACK_SECTORS ? 2048 : 2352;
	check(request->interface_id == 'S' &&
			request->dxfer_direction == SG_DXFER_FROM_DEV &&
			request->timeout > 0,
		"bounded read-only SCSI request");
	check(request->cmd_len == 12 && command[0] == GPCMD_READ_CD &&
			command[1] == (stride == 2048 ? 8 : 4) &&
			command[9] == 0x10 && command[10] == 0,
		"MMC selects Mode 1/CDDA payload without headers or subchannels");
	check(count > 0 && count <= MH_OPTICAL_READ_SECTORS &&
			request->dxfer_len == count * stride &&
			sector % TRACK_SECTORS + count <= TRACK_SECTORS,
		"device transfer stays inside requested track");

	if (device.failed_reads > 0) {
		device.failed_reads--;
		errno = device.read_error;
		return -1;
	}

	request->resid = device.residual;
	request->info = device.scsi_error ? SG_INFO_CHECK : SG_INFO_OK;
	bytes = request->dxferp;

	for (i = 0; i < request->dxfer_len; i++)
		bytes[i] = (unsigned char)((sector * stride + i) % 251);

	return 0;
}

int
__wrap_ioctl(int fd, unsigned long operation, ...)
{
	va_list arguments;
	void *argument;
	struct cdrom_tochdr *header;
	struct cdrom_tocentry *entry;

	check(fd == DEVICE_FD, "ioctl uses the opened CD device");

	if (operation == CDROM_GET_CAPABILITY)
		return CDC_GENERIC_PACKET;

	va_start(arguments, operation);
	argument = va_arg(arguments, void *);
	va_end(arguments);

	if (operation == SG_IO)
		return transfer(argument);

	if (operation == CDROMREADTOCHDR) {
		header = argument;
		header->cdth_trk0 = 1;
		header->cdth_trk1 =
			device.invalid_toc == TOC_BAD_COUNT ? 100 : 2;
		return 0;
	}

	if (operation == CDROMREADTOCENTRY) {
		entry = argument;
		check(entry->cdte_format == CDROM_LBA,
			"TOC uses logical sectors");
		entry->cdte_ctrl =
			entry->cdte_track == 1 ? CDROM_DATA_TRACK : 0;

		if (device.invalid_toc == TOC_EMPHASIS &&
			entry->cdte_track == 2)
			entry->cdte_ctrl = 1;

		entry->cdte_addr.lba = entry->cdte_track == CDROM_LEADOUT
			? 2 * TRACK_SECTORS
			: (entry->cdte_track - 1) * TRACK_SECTORS;

		if (device.invalid_toc == TOC_NEGATIVE_START)
			entry->cdte_addr.lba = -1;
		else if (device.invalid_toc == TOC_UNORDERED)
			entry->cdte_addr.lba = 0;

		return 0;
	}

	check(0, "unexpected device operation");
	errno = ENOTTY;

	return -1;
}

static void
check_open_failures(void)
{
	static const struct open_failure_case cases[] = {
		{TOC_BAD_COUNT, "invalid TOC count closes the device"},
		{TOC_NEGATIVE_START, "negative track start closes the device"},
		{TOC_UNORDERED, "unordered track boundaries close the device"},
		{TOC_EMPHASIS,
			"unsupported audio processing closes the device"}};

	struct mh_optical_disc *disc;
	int matches;
	size_t i;

	memset(&device, 0, sizeof(device));
	device.regular_file = 1;
	check(mh_linux_optical_probe("file", &matches, NULL) == MH_ERROR_NONE &&
			!matches,
		"regular files stay on the image path");
	disc = NULL;
	check(mh_linux_optical_open("file", &disc, NULL) ==
				MH_ERROR_UNSUPPORTED &&
			disc == NULL && device.closes == 1,
		"open checks device type again and releases rejected handle");

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(&device, 0, sizeof(device));
		device.invalid_toc = cases[i].fault;
		check(mh_linux_optical_open("cd", &disc, NULL) !=
					MH_ERROR_NONE &&
				disc == NULL && device.closes == 1,
			cases[i].reason);
	}

	memset(&device, 0, sizeof(device));
	device.open_error = EACCES;
	check(mh_linux_optical_open("cd", &disc, NULL) == MH_ERROR_IO &&
			device.closes == 0 && disc == NULL,
		"permission failure preserves output without closing an unowned fd");
}

static void
check_reads(void)
{
	struct mh_optical_disc *disc;
	const struct mh_resource_source *source;
	unsigned char bytes[4096];
	size_t i;

	memset(&device, 0, sizeof(device));
	disc = NULL;
	check(mh_linux_optical_open("cd", &disc, NULL) == MH_ERROR_NONE,
		"open a mixed-mode disc");

	if (disc == NULL)
		return;

	source = mh_optical_disc_resources(disc);
	device.failed_reads = 2;
	device.read_error = EIO;
	check(mh_resource_source_read_at(source, "track01", 62UL * 2048, bytes,
		sizeof(bytes), NULL) == MH_ERROR_NONE &&
			device.reads == 3,
		"retry transient read failure at the data/audio boundary");
	check(mh_resource_source_read_at(source, "track02", 0, bytes,
		sizeof(bytes), NULL) == MH_ERROR_NONE,
		"audio uses its absolute track start");

	for (i = 0; i < sizeof(bytes); i++) {
		if (bytes[i] !=
			(unsigned char)((TRACK_SECTORS * 2352UL + i) % 251)) {
			check(0, "audio preserves byte order");
			break;
		}
	}

	mh_optical_disc_close(disc);
	check(device.closes == 1, "successful open closes exactly once");
}

static void
check_transfer_failures(void)
{
	static const struct transfer_failure_case cases[] = {
		{EIO, 10, 0, 0, 3, "give up after three I/O failures"},
		{EINTR, 10, 0, 0, 1, "do not restart an interrupted read"},
		{0, 0, 1, 0, 3, "short transfers never succeed"},
		{0, 0, 0, 1, 3, "SCSI errors never succeed"}};

	struct mh_optical_disc *disc;
	unsigned char bytes[2];
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(&device, 0, sizeof(device));
		device.failed_reads = cases[i].failed_reads;
		device.read_error = cases[i].error;
		device.residual = cases[i].residual;
		device.scsi_error = cases[i].scsi_error;
		disc = NULL;
		check(mh_linux_optical_open("cd", &disc, NULL) == MH_ERROR_NONE,
			"open transfer-failure fixture");

		if (disc == NULL)
			continue;

		check(mh_resource_source_read_at(
			mh_optical_disc_resources(disc), "track01", 0,
			bytes, sizeof(bytes), NULL) == MH_ERROR_IO &&
				device.reads == cases[i].attempts,
			cases[i].reason);
		mh_optical_disc_close(disc);
	}
}

int
main(void)
{
	check_open_failures();
	check_reads();
	check_transfer_failures();

	return failures != 0;
}
