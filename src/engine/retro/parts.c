#include <string.h>

#include "engine/retro/parts.h"

void
mh_retro_part_store(struct mh_retro_assets *assets, enum mh_retro_part part,
	const union mh_retro_part_data *data)
{
	switch (part) {
	case MH_RETRO_PART_MESHES:
		memcpy(assets->meshes, data->meshes, sizeof(assets->meshes));
		break;

	case MH_RETRO_PART_ANIMATION:
		memcpy(assets->motion, data->motion, sizeof(assets->motion));
		break;

	case MH_RETRO_PART_PALETTE:
		memcpy(assets->palette, data->palette, sizeof(assets->palette));
		break;

	case MH_RETRO_PART_STORY:
		memcpy(assets->text, data->text, sizeof(assets->text));
		break;

	case MH_RETRO_PART_COUNT:
		break;
	}
}

void
mh_retro_part_copy(const struct mh_retro_assets *assets,
	enum mh_retro_part part, union mh_retro_part_data *data)
{
	memset(data, 0, sizeof(*data));

	switch (part) {
	case MH_RETRO_PART_MESHES:
		memcpy(data->meshes, assets->meshes, sizeof(assets->meshes));
		break;

	case MH_RETRO_PART_ANIMATION:
		memcpy(data->motion, assets->motion, sizeof(assets->motion));
		break;

	case MH_RETRO_PART_PALETTE:
		memcpy(data->palette, assets->palette, sizeof(assets->palette));
		break;

	case MH_RETRO_PART_STORY:
		memcpy(data->text, assets->text, sizeof(assets->text));
		break;

	case MH_RETRO_PART_COUNT:
		break;
	}
}
