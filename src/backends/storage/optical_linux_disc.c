#define _POSIX_C_SOURCE 200809L

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <linux/cdrom.h>

#include <errno.h>
#include <fcntl.h>
#include <scsi/sg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/optical_linux_provider.h"
#include "interfaces/storage/optical_backend.h"

enum {
	READ_ATTEMPTS = 3,
	READ_TIMEOUT_MS = 10000,
	AUDIO_PRE_EMPHASIS = 0x01,
	AUDIO_FOUR_CHANNEL = 0x08,
	MMC_CDDA = 1,
	MMC_MODE1 = 2
};

struct linux_disc {
	int			fd;
	unsigned long		starts[MH_OPTICAL_TRACK_LIMIT];
	struct mh_optical_track	tracks[MH_OPTICAL_TRACK_LIMIT];
	size_t			count;
};

enum mh_error_code
mh_linux_optical_probe(
	const char *path, int *matches, struct mh_error *error_out)
{
	struct stat info;

	if (path == NULL || matches == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid CD device probe arguments");

	if (stat(path, &info) != 0) {
		if (errno != ENOENT && errno != ENOTDIR)
			return mh_error_set(error_out, MH_ERROR_IO,
				"cannot inspect source path");

		*matches = 0;
	} else
		*matches = S_ISBLK(info.st_mode);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_toc_entry(int fd, unsigned int number, struct cdrom_tocentry *entry,
	struct mh_error *error_out)
{
	memset(entry, 0, sizeof(*entry));

	entry->cdte_track = (unsigned char)number;
	entry->cdte_format = CDROM_LBA;

	if (ioctl(fd, CDROMREADTOCENTRY, entry) < 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot read CD track boundaries");

	if (entry->cdte_addr.lba < 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"CD track starts before the supported data area");

	/*
	 * The PCM contract does not apply de-emphasis or decode four channels.
	 */
	if (number != CDROM_LEADOUT &&
		(entry->cdte_ctrl & CDROM_DATA_TRACK) == 0 &&
		(entry->cdte_ctrl &
			(AUDIO_PRE_EMPHASIS | AUDIO_FOUR_CHANNEL)) != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"CD audio requires unsupported emphasis or channel processing");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_toc(struct linux_disc *disc, struct mh_error *error_out)
{
	struct cdrom_tochdr header;
	struct cdrom_tocentry entry;
	unsigned long start;
	unsigned int number;
	size_t i;
	enum mh_error_code status;

	if (ioctl(disc->fd, CDROMREADTOCHDR, &header) < 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot read CD table of contents; insert a readable game CD");

	if (header.cdth_trk0 < 1 || header.cdth_trk1 < header.cdth_trk0 ||
		header.cdth_trk1 > MH_OPTICAL_TRACK_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid CD track count");

	disc->count = (size_t)(header.cdth_trk1 - header.cdth_trk0) + 1;

	for (i = 0; i <= disc->count; i++) {
		number = i == disc->count ? CDROM_LEADOUT
					: header.cdth_trk0 + (unsigned int)i;
		status = read_toc_entry(disc->fd, number, &entry, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		start = (unsigned long)entry.cdte_addr.lba;

		if (i != 0) {
			if (start <= disc->starts[i - 1])
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"CD track boundaries are not increasing");

			disc->tracks[i - 1].sectors =
				start - disc->starts[i - 1];
		}

		if (i == disc->count)
			break;

		disc->starts[i] = start;
		disc->tracks[i].number = number;
		disc->tracks[i].kind = (entry.cdte_ctrl & CDROM_DATA_TRACK)
			? MH_OPTICAL_DATA
			: MH_OPTICAL_AUDIO;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
read_blocks(int fd, enum mh_optical_track_kind kind, unsigned long sector,
	unsigned int count, void *output)
{
	unsigned char command[12], sense[32];
	sg_io_hdr_t request;

	/*
	 * Read exactly the requested sectors. Buffered block-device reads can
	 * read ahead into the audio pregap and fail at the end of data tracks.
	 * MMC returns audio as little-endian PCM regardless of CPU byte order.
	 */
	memset(command, 0, sizeof(command));
	command[0] = GPCMD_READ_CD;
	command[1] = (kind == MH_OPTICAL_DATA ? MMC_MODE1 : MMC_CDDA) << 2;
	command[2] = (unsigned char)(sector >> 24);
	command[3] = (unsigned char)(sector >> 16);
	command[4] = (unsigned char)(sector >> 8);
	command[5] = (unsigned char)sector;
	command[8] = (unsigned char)count;
	command[9] = 0x10; /* User data only; no headers, ECC or subchannels. */
	memset(&request, 0, sizeof(request));
	memset(sense, 0, sizeof(sense));
	request.interface_id = 'S';
	request.dxfer_direction = SG_DXFER_FROM_DEV;
	request.cmd_len = sizeof(command);
	request.cmdp = command;
	request.mx_sb_len = sizeof(sense);
	request.sbp = sense;
	request.dxfer_len = count *
		(kind == MH_OPTICAL_DATA ? MH_OPTICAL_DATA_BYTES
					: MH_OPTICAL_AUDIO_BYTES);
	request.dxferp = output;
	request.timeout = READ_TIMEOUT_MS;

	if (ioctl(fd, SG_IO, &request) < 0)
		return -1;

	if ((request.info & SG_INFO_OK_MASK) != SG_INFO_OK ||
		request.resid != 0) {
		errno = EIO;
		return -1;
	}

	return 0;
}

static enum mh_error_code
read_sectors(void *context, size_t index, unsigned long sector,
	unsigned int count, void *output, struct mh_error *error_out)
{
	struct linux_disc *disc;
	unsigned int attempt;
	int result;

	disc = context;
	sector += disc->starts[index];

	for (attempt = 0; attempt < READ_ATTEMPTS; attempt++) {
		result = read_blocks(disc->fd, disc->tracks[index].kind, sector,
			count, output);

		if (result == 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (errno != EIO)
			break;
	}

	if (errno == EINTR)
		return mh_error_set(
			error_out, MH_ERROR_IO, "CD read interrupted");

	return mh_error_set(error_out, MH_ERROR_IO,
		"cannot read CD sectors; check the disc and drive");
}

static void
close_disc(void *context)
{
	struct linux_disc *disc;

	disc = context;
	close(disc->fd);
	free(disc);
}

static enum mh_error_code
open_device(const char *path, int *output, struct mh_error *error_out)
{
	struct stat info;
	int fd;

	/* A normal read-only open lets the kernel hold the tray until close. */
	fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			errno == EACCES
				? "permission denied opening CD device"
				: "cannot open CD device; insert a readable game CD");

	if (fstat(fd, &info) != 0 || !S_ISBLK(info.st_mode) ||
		ioctl(fd, CDROM_GET_CAPABILITY, 0) < 0) {
		close(fd);
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"source is not a supported CD device");
	}

	*output = fd;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_linux_optical_open(const char *path, struct mh_optical_disc **output,
	struct mh_error *error_out)
{
	static const struct mh_optical_ops ops = {read_sectors, close_disc};
	struct linux_disc *disc;
	enum mh_error_code status;

	if (path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid CD device open arguments");

	disc = calloc(1, sizeof(*disc));

	if (disc == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening CD device");

	status = open_device(path, &disc->fd, error_out);

	if (status != MH_ERROR_NONE) {
		free(disc);
		return status;
	}

	status = read_toc(disc, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_optical_disc_create(&ops, disc, disc->tracks,
			disc->count, output, error_out);

	if (status != MH_ERROR_NONE)
		close_disc(disc);

	return status;
}
