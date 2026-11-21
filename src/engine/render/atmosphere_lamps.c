#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/atmosphere_internal.h"
#include "engine/render/atmosphere_lamps.h"
#include "interfaces/image/decoder.h"

/* Each source submits its seven reflections before the two glow slots. */
static void
init_draws(struct mh_draw *draws, size_t count)
{
	size_t source;

	for (source = 0; source < count; source++) {
		draws[source * MH_LAMP_DRAWS].first = source * LAMP_VERTICES;
		draws[source * MH_LAMP_DRAWS].count = FLARE_VERTICES;
		draws[source * MH_LAMP_DRAWS].texture = 1;
		draws[source * MH_LAMP_DRAWS].prelit = 1;
		draws[source * MH_LAMP_DRAWS + 1].first =
			source * LAMP_VERTICES + FLARE_VERTICES;
		draws[source * MH_LAMP_DRAWS + 1].count = GLOW_VERTICES;
		draws[source * MH_LAMP_DRAWS + 1].prelit = 1;
	}
}

enum mh_error_code
mh_halo_mesh_create(const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world, const unsigned char *data, size_t size,
	struct mh_mesh *output, struct mh_error *error_out)
{
	struct mh_mesh mesh;
	enum mh_error_code status;
	size_t count;

	if (world == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing halo world");

	count = world->halo_count;

	if (count > MH_LAMP_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many world halos");

	status = mh_atmosphere_mesh_allocate(
		(count == 0 ? 1 : count) * GLOW_VERTICES, 1, 1, &mesh,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_image_decode(
		decoder, data, size, NULL, &mesh.textures[0], NULL, error_out);

	if (status == MH_ERROR_NONE &&
		(mesh.textures[0].width != GLOW_TEXTURE_SIZE ||
			mesh.textures[0].height != GLOW_TEXTURE_SIZE))
		status = mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported halofix dimensions (expected 128x128)");

	if (status != MH_ERROR_NONE) {
		mh_mesh_free(&mesh);
		return status;
	}

	mesh.draws[0].count = mesh.vertex_count;
	mesh.draws[0].prelit = 1;
	*output = mesh;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_lamps_free(struct mh_lamps *lamps)
{
	if (lamps == NULL)
		return;

	mh_mesh_free(&lamps->mesh);
	free(lamps->probes);
	free(lamps->visible);
	free(lamps->world_hidden);
	memset(lamps, 0, sizeof(*lamps));
}

enum mh_error_code
mh_lamps_create(const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world, const unsigned char *glow,
	size_t glow_size, const unsigned char *hex, size_t hex_size,
	struct mh_lamps *output, struct mh_error *error_out)
{
	struct mh_lamps lamps;
	size_t count;
	enum mh_error_code status;

	if (world == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing lamp world/output");

	count = world->halo_count;

	if (count > MH_LAMP_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many world lamps");

	memset(&lamps, 0, sizeof(lamps));
	lamps.count = lamps.world_count = count;
	lamps.halos = lamps.flares = 1;

	if (count == 0)
		count = 1;

	status = mh_atmosphere_mesh_allocate(count * LAMP_VERTICES,
		count * MH_LAMP_DRAWS, LAMP_TEXTURES, &lamps.mesh, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	lamps.probes =
		(struct mh_depth_probe *)calloc(count, sizeof(*lamps.probes));
	lamps.visible = (unsigned char *)calloc(count, 1);
	lamps.world_hidden = (unsigned char *)calloc(count, 1);

	if (lamps.probes == NULL || lamps.visible == NULL ||
		lamps.world_hidden == NULL)
		status = mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory for lamp visibility");

	if (status == MH_ERROR_NONE)
		status = mh_image_decode(decoder, glow, glow_size, NULL,
			&lamps.mesh.textures[0], NULL, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_image_decode(decoder, hex, hex_size, NULL,
			&lamps.mesh.textures[1], NULL, error_out);

	if (status == MH_ERROR_NONE &&
		(lamps.mesh.textures[0].width != GLOW_TEXTURE_SIZE ||
			lamps.mesh.textures[0].height != GLOW_TEXTURE_SIZE ||
			lamps.mesh.textures[1].width != FLARE_TEXTURE_SIZE ||
			lamps.mesh.textures[1].height != FLARE_TEXTURE_SIZE))
		status = mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported lamp texture size");

	if (status != MH_ERROR_NONE) {
		mh_lamps_free(&lamps);
		return status;
	}

	/*
	 * Normalize the initial nonzero lamp history to true.
	 */
	memset(lamps.visible, 1, count);

	init_draws(lamps.mesh.draws, count);

	*output = lamps;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lamps_reserve_attached(struct mh_lamps *lamps, size_t attached_count,
	struct mh_error *error_out)
{
	struct mh_vertex *vertices;
	struct mh_draw *draws;
	struct mh_depth_probe *probes;
	unsigned char *visible;
	float *colors;
	size_t source_count, capacity, retained_count;

	if (lamps == NULL || lamps->world_count > MH_LAMP_LIMIT ||
		lamps->count < lamps->world_count ||
		attached_count > MH_LAMP_LIMIT - lamps->world_count ||
		lamps->mesh.texture_count != LAMP_TEXTURES ||
		lamps->probes == NULL || lamps->visible == NULL)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid attached lamp capacity");

	source_count = lamps->world_count + attached_count;

	if (source_count == lamps->count)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	capacity = source_count == 0 ? 1 : source_count;

	if (capacity > (size_t)-1 / (LAMP_VERTICES * sizeof(*vertices)))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"too many attached lamp vertices");

	colors = NULL;

	if (lamps->profile == MH_RENDER_POWERVR)
		colors = (float *)calloc(
			capacity * LAMP_VERTICES, 3 * sizeof(*colors));

	vertices = (struct mh_vertex *)calloc(
		capacity * LAMP_VERTICES, sizeof(*vertices));
	draws = (struct mh_draw *)calloc(
		capacity * MH_LAMP_DRAWS, sizeof(*draws));
	probes = (struct mh_depth_probe *)calloc(capacity, sizeof(*probes));
	visible = (unsigned char *)malloc(capacity);

	if (vertices == NULL || draws == NULL || probes == NULL ||
		visible == NULL ||
		(lamps->profile == MH_RENDER_POWERVR && colors == NULL)) {
		free(vertices);
		free(draws);
		free(probes);
		free(visible);
		free(colors);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory for attached lamps");
	}

	memset(visible, 1, capacity);
	retained_count =
		source_count < lamps->count ? source_count : lamps->count;

	if (retained_count != 0)
		memcpy(visible, lamps->visible, retained_count);

	init_draws(draws, capacity);

	free(lamps->mesh.material_rgb);
	lamps->mesh.material_rgb = colors;
	lamps->mesh.material_rgb_count =
		colors == NULL ? 0 : capacity * LAMP_VERTICES;
	free(lamps->mesh.vertices);
	free(lamps->mesh.draws);
	free(lamps->probes);
	free(lamps->visible);
	lamps->mesh.vertices = vertices;
	lamps->mesh.vertex_count = capacity * LAMP_VERTICES;
	lamps->mesh.draws = draws;
	lamps->mesh.draw_count = capacity * MH_LAMP_DRAWS;
	lamps->probes = probes;
	lamps->visible = visible;
	lamps->count = source_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
