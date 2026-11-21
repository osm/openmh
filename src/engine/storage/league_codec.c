#include <stdlib.h>

#include "engine/assets/tbl.h"
#include "engine/storage/league_codec.h"

enum mh_error_code
mh_league_bytes_decode(const struct mh_file *file, const char *name,
	const struct mh_profile *profiles, unsigned int count,
	struct mh_league_document *output, struct mh_error *error_out)
{
	unsigned char *text;
	size_t size;
	enum mh_error_code status;

	if (file->size > MH_LEAGUE_FILE_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "league file too large");

	status = mh_tbl_decode_named(
		file->data, file->size, name, &text, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_league_load_text(
		text, size, profiles, count, output, error_out);
	free(text);

	return status;
}

enum mh_error_code
mh_league_bytes_encode(const struct mh_league_document *document,
	const char *name, struct mh_file *output, struct mh_error *error_out)
{
	struct mh_file encoded;
	unsigned char *text;
	size_t size;
	enum mh_error_code status;

	status = mh_league_save_text(&document->league, document->profiles,
		MH_LEAGUE_ENTRIES, document->name, document->created,
		document->races, document->laps, &text, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_tbl_encode_named(
		text, size, name, &encoded.data, &encoded.size, error_out);
	free(text);

	if (status == MH_ERROR_NONE)
		*output = encoded;

	return status;
}
