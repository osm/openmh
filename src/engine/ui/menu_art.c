#include "engine/ui/menu_art.h"

#define SPRITE_DIMENSION_LIMIT 4096
#define ART_BYTE_LIMIT 33554432UL

enum mh_error_code
mh_menu_art_read(struct mh_menu_art_reader *reader,
	const struct mh_sprite_collection *collection, const char *name,
	struct mh_sprite_indexed *images, unsigned int capacity,
	enum mh_menu_art_frames frames, unsigned int *count,
	struct mh_error *error_out)
{
	struct mh_sprite_info info;
	enum mh_error_code status;
	unsigned int i, frame_count;
	size_t bytes;

	status = mh_sprite_collection_info(collection, name, &info, error_out);

	if (count != NULL)
		*count = 0;

	if (status == MH_ERROR_NOT_FOUND)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (status != MH_ERROR_NONE)
		return status;

	frame_count = frames == MH_MENU_ART_FIRST_FRAME ? 1 : info.frames;

	if (info.frames == 0 || frame_count > capacity || info.width == 0 ||
		info.width > SPRITE_DIMENSION_LIMIT || info.height < 2 ||
		info.height > SPRITE_DIMENSION_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, reader->invalid_sprite);

	bytes = (size_t)info.width * info.height * 2;

	if (reader->used > ART_BYTE_LIMIT ||
		frame_count > (ART_BYTE_LIMIT - reader->used) / bytes)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, reader->capacity_error);

	reader->used += bytes * frame_count;

	if (count != NULL)
		*count = frame_count;

	for (i = 0; i < frame_count && status == MH_ERROR_NONE; i++)
		status = mh_sprite_collection_indexed(
			collection, name, i, &images[i], error_out);

	return status;
}
