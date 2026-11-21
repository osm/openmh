#ifndef MH_EXTRACT_DISC_INTERNAL_H
#define MH_EXTRACT_DISC_INTERNAL_H

#include "engine/assets/prepare_disc_layout.h"
#include "extract/progress/report.h"

/* Shared by image-file and physical-drive extraction; borrows all inputs. */
enum mh_error_code	mh_extract_disc_archive(struct mh_archive *archive,
	const char *staging, mh_extract_progress progress, void *context,
	struct mh_error *error_out);
enum mh_error_code	mh_extract_disc_audio(
	const struct mh_resource_source *resources, const char *layout_path,
	const struct mh_disc_layout *layout, const char *staging,
	mh_extract_progress progress, void *context,
	struct mh_error *error_out);

#endif
