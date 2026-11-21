#include <stdio.h>
#include <string.h>

#include "engine/archive/iso.h"
#include "extract/hosts/optical.h"
#include "extract/source/disc_internal.h"
#include "extract/source/drive.h"

enum {
	LARGE_FILE_BYTES = 4 * 1024 * 1024,
	PROGRESS_PERCENT_STEP = 10
};

struct drive_progress {
	mh_extract_progress		report;
	void				*context;
	unsigned int			track, percent;
	struct mh_archive		*archive;
	const struct mh_archive_entry	*file;
	unsigned long			first_sector;
	unsigned int			file_percent;
	int				file_started;
};

static enum mh_error_code
finish_file(struct drive_progress *progress, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = MH_ERROR_NONE;

	if (progress->file != NULL && progress->report != NULL)
		status = progress->report(progress->context,
			MH_EXTRACT_DISC_FILE, progress->file->path, 100, 100,
			error_out);

	progress->file = NULL;

	return status;
}

static enum mh_error_code
report_member(void *context, enum mh_extract_step step, const char *path,
	size_t completed, size_t total, struct mh_error *error_out)
{
	struct drive_progress *progress;
	const struct mh_archive_entry *entry;
	enum mh_error_code status;

	progress = context;

	/* The next member is reached only after the previous one succeeded. */
	status = finish_file(progress, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (progress->report != NULL) {
		status = progress->report(progress->context, step, path,
			completed, total, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (path == NULL) {
		progress->archive = NULL;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (progress->archive == NULL || completed >= total ||
		total != mh_archive_count(progress->archive))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	entry = mh_archive_entry(progress->archive, completed);

	if (entry->size < LARGE_FILE_BYTES || strcmp(entry->path, path) != 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	progress->file = entry;
	progress->file_started = 0;
	progress->file_percent = 0;

	if (progress->report != NULL)
		return progress->report(progress->context, MH_EXTRACT_DISC_FILE,
			path, 0, 100, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
report_file_read(struct drive_progress *progress, unsigned long sector,
	struct mh_error *error_out)
{
	unsigned long bytes;
	unsigned int percent;

	if (progress->file == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!progress->file_started) {
		progress->first_sector = sector;
		progress->file_started = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/* ISO members are contiguous; track position alone is not progress. */
	if (sector < progress->first_sector)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	bytes = (sector - progress->first_sector) * MH_OPTICAL_DATA_BYTES;
	percent = (unsigned int)((double)bytes * 100.0 /
			(double)progress->file->size) /
		PROGRESS_PERCENT_STEP;

	if (percent <= progress->file_percent ||
		percent >= 100 / PROGRESS_PERCENT_STEP)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	progress->file_percent = percent;

	if (progress->report != NULL)
		return progress->report(progress->context, MH_EXTRACT_DISC_FILE,
			progress->file->path, percent * PROGRESS_PERCENT_STEP,
			100, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
report_read(void *context, unsigned int track, unsigned long sector,
	unsigned long total, struct mh_error *error_out)
{
	struct drive_progress *progress;
	unsigned int percent;
	char name[16];
	enum mh_error_code status;

	progress = context;

	if (progress->report != NULL) {
		status = progress->report(progress->context, MH_EXTRACT_POLL,
			NULL, 0, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (track == 1)
		return report_file_read(progress, sector, error_out);

	/* Data files can seek backwards; only audio is a sequential rip. */
	percent = (unsigned int)((double)sector * 100.0 / (double)total) / 10;

	if (progress->report != NULL && percent > 0 && percent < 10 &&
		(track != progress->track || percent != progress->percent)) {
		sprintf(name, "%02u", track);
		status = progress->report(progress->context, MH_EXTRACT_AUDIO,
			name, percent, 10, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	progress->track = track;
	progress->percent = percent;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
retail_layout(const struct mh_optical_disc *disc, struct mh_disc_layout *layout,
	struct mh_error *error_out)
{
	const struct mh_optical_track *track;
	unsigned int i;

	if (mh_optical_disc_count(disc) != MH_RETAIL_DISC_TRACKS)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected the retail CD with one data track and ten audio tracks");

	memset(layout, 0, sizeof(*layout));
	layout->data_format = MH_DISC_ISO;

	for (i = 0; i < MH_RETAIL_DISC_TRACKS; i++) {
		track = mh_optical_disc_track(disc, i);

		if (track->number != i + 1 ||
			track->kind !=
				(i == 0 ? MH_OPTICAL_DATA : MH_OPTICAL_AUDIO))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported retail CD track order");

		sprintf(layout->tracks[i].file, "track%02u", track->number);
		layout->tracks[i].length = track->sectors *
			(i == 0 ? MH_OPTICAL_DATA_BYTES
				: MH_OPTICAL_AUDIO_BYTES);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_extract_drive_unpack(const char *path, const char *staging,
	int include_music, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	struct mh_optical_disc *disc;
	struct mh_archive *archive;
	struct mh_disc_layout layout;
	struct drive_progress observer;
	const struct mh_resource_source *resources;
	enum mh_error_code status;

	disc = NULL;
	archive = NULL;
	memset(&observer, 0, sizeof(observer));
	observer.report = progress;
	observer.context = context;
	status = mh_extract_optical_open(path, &disc, error_out);

	if (status == MH_ERROR_NONE)
		status = retail_layout(disc, &layout, error_out);

	resources = mh_optical_disc_resources(disc);
	mh_optical_disc_set_progress(disc, report_read, &observer);

	if (status == MH_ERROR_NONE)
		status = mh_iso_open(resources, layout.tracks[0].file,
			MH_DISC_ISO, &archive, error_out);

	if (status == MH_ERROR_NONE) {
		observer.archive = archive;
		status = mh_extract_disc_archive(
			archive, staging, report_member, &observer, error_out);
	}

	if (status == MH_ERROR_NONE && include_music)
		status = mh_extract_disc_audio(resources, "", &layout, staging,
			progress, context, error_out);

	mh_archive_close(archive);
	mh_optical_disc_close(disc);

	return status;
}
