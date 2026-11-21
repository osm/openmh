#ifndef MH_RETRO_ASSETS_H
#define MH_RETRO_ASSETS_H

#include "engine/core/error.h"
#include "engine/core/file.h"
#include "interfaces/retro/data.h"

enum {
	MH_RETRO_FILE_LIMIT = 16384
};

/* Owned files produced by extraction or collected by the MHRETRO provider. */
struct mh_retro_files {
	struct mh_file	parts[MH_RETRO_PART_COUNT];
};

const char	*mh_retro_mhretro_filename(enum mh_retro_part part);
void		 mh_retro_mhretro_free(struct mh_retro_files *files);

/* Decode the four independent files; failures preserve output. */
enum mh_error_code	mh_retro_mhretro_decode(
	const struct mh_retro_files *files, struct mh_retro_assets *output,
	struct mh_error *error_out);

/* Decode/check one independent file. Decode failure preserves output. */
enum mh_error_code	mh_retro_mhretro_decode_part(enum mh_retro_part part,
	const unsigned char *data, size_t size,
	union mh_retro_part_data *output, struct mh_error *error_out);
enum mh_error_code	mh_retro_mhretro_check(enum mh_retro_part part,
	const unsigned char *data, size_t size, struct mh_error *error_out);

#endif
