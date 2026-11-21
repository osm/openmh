#ifndef MH_UNSHIELD_PROVIDER_H
#define MH_UNSHIELD_PROVIDER_H

#include "interfaces/archive/archive.h"

/*
 * Single-volume cabinet; bytes stay borrowed/unchanged until archive close.
 * Call only after identifying the supported source package. This decoder is
 * not a sandbox for arbitrary installer bytes. No native paths are opened.
 */
enum mh_error_code	mh_unshield_open(const void *bytes, size_t size,
	struct mh_archive **output, struct mh_error *error_out);

/*
 * Preserve installer groups so alternate editions cannot overwrite each other.
 */
enum mh_error_code	mh_unshield_open_components(const void *bytes,
	size_t size, struct mh_archive **output, struct mh_error *error_out);

/*
 * Identified demo SFX records, excluding the executable stub. Bytes remain
 * borrowed until close. compressed selects the MPlayer per-member ZIP wrapper.
 * Original build-machine paths become disk1/<original filename>.
 */
enum mh_error_code	mh_unshield_sfx_open(const void *bytes, size_t size,
	int compressed, struct mh_archive **output, struct mh_error *error_out);

/*
 * Decode the data1.cab ZIP member from its local header. Does not
 * search a ZIP directory (the MPlayer installer concatenates ZIP archives).
 * Validates lengths, full Deflate consumption and CRC; returns owned bytes.
 * Use only for the identified package's member. Output is unchanged on error.
 */
enum mh_error_code	mh_unshield_zip_cabinet(const void *bytes, size_t size,
	struct mh_file *output, struct mh_error *error_out);

#endif
