#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/image/remap_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/assets/myo.h"
#include "engine/core/text.h"
#include "engine/geometry/model.h"
#include "engine/image/collection_load.h"
#include "engine/render/scene_internal.h"
#include "interfaces/image/collection_backend.h"
#include "interfaces/image/decoder_backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "image collections:%d: %s\n",          \
				__LINE__, #e);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

#define TEST_TEXTURE_BUDGET (32UL * 1024 * 1024)

enum {
	REMAP_HEADER = 28,
	REMAP_ENTRY = 49,
	REMAP_BYTES = REMAP_HEADER + 2 * REMAP_ENTRY
};

struct fixture {
	unsigned int	opens, reads, closes, fail_at;
	unsigned char	primary, secondary;
};

static struct mh_race_scene scene;

static enum mh_error_code
open_set(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)resources;
	fixture = context;
	fixture->opens++;

	if (strcmp(root, "root") != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unexpected test root");

	if (strcmp(reference, "Tracks/custom/Packed/tex1.pdi") == 0)
		*output = &fixture->primary;
	else if (strcmp(reference, "Tracks/custom/Packed/tex2.pdi") == 0)
		*output = &fixture->secondary;
	else
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"unknown test collection");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_member(void *context, void *state, const char *name,
	struct mh_file *output, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_file file;

	fixture = context;
	fixture->reads++;

	if (fixture->reads == fixture->fail_at)
		return mh_error_set(
			error_out, MH_ERROR_IO, "test member failure");

	if (!mh_ascii_equal(name, "opaque.iff") &&
		!mh_ascii_equal(name, "cutout.iff"))
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "unknown test image");

	file.data = malloc(1);

	if (file.data == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test member allocation");

	file.data[0] = *(unsigned char *)state;
	file.size = 1;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_set(void *context, void *state)
{
	struct fixture *fixture;

	(void)state;
	fixture = context;
	fixture->closes++;
}

static enum mh_error_code
read_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	struct mh_indexed_image image;
	unsigned int i;

	(void)context;

	if (size != 1)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid test image");

	memset(&image, 0, sizeof(image));
	image.width = image.height = 2;
	image.size = 4;
	image.colors = 2;
	image.indices = malloc(4);

	if (image.indices == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test index allocation");

	memcpy(image.indices, "\0\1\1\0", 4);

	for (i = 0; i < 2; i++) {
		image.palette[i * 3] = data[0];
		image.palette[i * 3 + 1] = 200;
		image.palette[i * 3 + 2] = 51;
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette, struct mh_error *error_out)
{
	struct mh_image image;
	struct mh_indexed_image indexed;
	enum mh_error_code status;
	unsigned int i;

	if (options->alpha != MH_IMAGE_ALPHA_INDEX_ZERO ||
		options->palette != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected texture policy");

	status = read_indexed(context, data, size, &indexed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	image.width = image.height = 2;
	image.rgba = malloc(16);

	if (image.rgba == NULL) {
		free(indexed.indices);
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test RGBA allocation");
	}

	for (i = 0; i < 4; i++) {
		memcpy(image.rgba + i * 4,
			indexed.palette + indexed.indices[i] * 3, 3);
		image.rgba[i * 4 + 3] = indexed.indices[i] == 0 ? 0 : 255;
	}

	if (palette != NULL) {
		palette->count = indexed.colors;
		memcpy(palette->rgb, indexed.palette, sizeof(palette->rgb));
	}

	free(indexed.indices);
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
write_u32(unsigned char *bytes, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		bytes[i] = (unsigned char)(value >> (i * 8));
}

/* One textured quad with four distinct positions/UVs and a flat normal. */
static void
make_model(unsigned char *data)
{
	static const unsigned long counts[7] = {4, 1, 1, 1, 1, 4, 1};
	static const unsigned long lengths[7] = {48, 24, 16, 12, 12, 8, 11};
	static const unsigned long offsets[7] = {
		124, 172, 196, 212, 224, 236, 244};

	int i;

	memset(data, 0, 255);
	memcpy(data, "2OYM", 4);

	for (i = 0; i < 7; i++) {
		write_u32(data + 4 + i * 12, counts[i]);
		write_u32(data + 8 + i * 12, lengths[i]);
		write_u32(data + 12 + i * 12, offsets[i]);
	}

	write_u32(data + 100, 255);
	write_u32(data + 124 + 12, 0x3f800000UL);
	write_u32(data + 124 + 24, 0x3f800000UL);
	write_u32(data + 124 + 28, 0x3f800000UL);
	write_u32(data + 124 + 40, 0x3f800000UL);
	data[172] = 3;
	data[182] = 1;
	data[184] = 2;
	data[186] = 3;
	data[176] = 17;
	data[177] = 64;
	data[178] = 129;

	/* +Z visibility plane, independent of normals. */
	write_u32(data + 204, 0x3f800000UL);
	write_u32(data + 220, 0x3f800000UL); /* +Z normal. */
	data[238] = 2;
	data[240] = 2;
	data[241] = 1;
	data[243] = 1;
	memcpy(data + 244, "cutout.iff", 11);
}

static int
check_mesh(const struct mh_image_source_provider *source,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources)
{
	struct mh_image_collection *cutout, *opaque;
	struct mh_model model;
	struct mh_mesh mesh;
	struct mh_track track;
	struct mh_indexed_image *retained;
	unsigned char bytes[255], *pixels;

	cutout = opaque = NULL;
	CHECK(mh_image_collection_open(source, resources, "root",
		"Tracks/custom/Packed/tex1.pdi", &cutout,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_image_collection_open(source, resources, "root",
		"Tracks/custom/Packed/tex2.pdi", &opaque,
		NULL) == MH_ERROR_NONE);
	make_model(bytes);
	CHECK(mh_model_myo_decode(bytes, sizeof(bytes), &model, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mesh_build_lods_profile(decoder, &model, 1, cutout,
		MH_RENDER_SOFTWARE, &mesh, NULL) == MH_ERROR_NONE);
	CHECK(mesh.texture_count == 2 && mesh.vertex_count == 6);
	CHECK(mesh.textures[1].width == 2 && mesh.textures[1].height == 2);
	CHECK(mesh.textures[1].rgba[0] == 40 && mesh.textures[1].rgba[3] == 0);
	CHECK(mesh.textures[1].rgba[7] == 255);
	CHECK(mesh.vertices[2].uv[0] == 1.25f &&
		mesh.vertices[2].uv[1] == .75f);
	CHECK(mh_mesh_load_indexed_textures(decoder, &mesh, cutout, NULL) ==
		MH_ERROR_NONE);
	CHECK(mesh.indexed_textures[1].indices[0] == 0 &&
		mesh.indexed_textures[1].indices[1] == 1);
	CHECK(memcmp(mesh.indexed_textures[1].palette,
		mesh.indexed_textures[1].palette + 3, 3) == 0);

	memset(&track, 0, sizeof(track));
	track.texture_brightness[0] = track.texture_brightness[1] =
		track.texture_brightness[2] = 1;
	CHECK(mh_mesh_prepare_track_textures(decoder, &mesh, cutout, opaque,
		&track, MH_TEXTURE_XRGB8888, MH_TEXTURE_ARGB1555,
		NULL) == MH_ERROR_NONE);
	CHECK(mesh.textures[1].rgba[0] == 80 &&
		mesh.textures[1].rgba[3] == 255);
	strcpy(track.zero_transparent, "cutout.iff");
	track.zero_transparent_size = 11;
	CHECK(mh_mesh_prepare_track_textures(decoder, &mesh, cutout, opaque,
		&track, MH_TEXTURE_XRGB8888, MH_TEXTURE_ARGB1555,
		NULL) == MH_ERROR_NONE);
	CHECK(mesh.textures[1].rgba[0] == 41 && mesh.textures[1].rgba[3] == 0);

	pixels = mesh.textures[1].rgba;
	retained = mesh.indexed_textures;
	strcpy(mesh.draws[0].texture_name, "missing.iff");
	CHECK(mh_mesh_prepare_track_textures(decoder, &mesh, cutout, opaque,
		&track, MH_TEXTURE_XRGB8888, MH_TEXTURE_ARGB1555,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_mesh_load_indexed_textures(decoder, &mesh, cutout, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(mesh.textures[1].rgba == pixels &&
		mesh.indexed_textures == retained);
	mh_image_collection_close(opaque);
	mh_image_collection_close(cutout);
	CHECK(pixels[7] == 255 && retained[1].indices[1] == 1);
	mh_mesh_free(&mesh);
	mh_model_free(&model);

	return 0;
}

static void
make_remap(unsigned char bytes[REMAP_BYTES])
{
	unsigned char *entry;
	unsigned int i;

	memset(bytes, 0, REMAP_BYTES);
	write_u32(bytes, 2);
	write_u32(bytes + 4, 2);
	write_u32(bytes + 8, 1);
	write_u32(bytes + 12, 4);
	write_u32(bytes + 16, 4);
	write_u32(bytes + 20, 0x3f800000UL);
	write_u32(bytes + 24, 0x3f800000UL);

	for (i = 0; i < 2; i++) {
		entry = bytes + REMAP_HEADER + i * REMAP_ENTRY;
		strcpy((char *)entry, i == 0 ? "opaque.iff" : "cutout.iff");
		write_u32(entry + 13, i);
		write_u32(entry + 21, 1);
		write_u32(entry + 25, 1);
		write_u32(entry + 33, 2);
		write_u32(entry + 37, 2);
		write_u32(entry + 41, 2);
		write_u32(entry + 45, 2);
	}
}

static void
init_scene(const struct mh_image_source_provider *source,
	const struct mh_image_decoder_provider *decoder,
	enum mh_render_profile profile)
{
	scene.image_source = source;

	scene.image_decoder = decoder;
	scene.atlas_source = mh_atlas_remap_source();
	scene.animation_source = mh_animation_lightwave_source();
	scene.navigation_source = mh_navigation_mot_dat_source();
	scene.catalog_source = mh_catalog_fallback_source();
	scene.material_source = mh_material_mat_srf_source();
	scene.env_sound_source = mh_env_sounds_esd_source();
	scene.horn_source = mh_horn_fallback_source();
	scene.assets.profile = profile;
	scene.assets.texture_budget = TEST_TEXTURE_BUDGET;
	scene.assets.opaque_format = profile == MH_RENDER_GLIDE
		? MH_TEXTURE_PALETTE_RGB
		: profile == MH_RENDER_DIRECT3D ? MH_TEXTURE_RGB565
						: MH_TEXTURE_RGB555;
	scene.assets.alpha_format = profile == MH_RENDER_POWERVR
		? MH_TEXTURE_ARGB4444
		: MH_TEXTURE_ARGB1555;
	strcpy(scene.assets.track.base_path, "Tracks/custom");
	strcpy(scene.assets.track.zero_transparent, "cutout.iff");
	scene.assets.track.zero_transparent_size = 11;
	scene.assets.track.texture_brightness[0] =
		scene.assets.track.texture_brightness[1] =
			scene.assets.track.texture_brightness[2] = 1;
}

static int
check_atlas(const struct mh_image_source_provider *source,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources,
	enum mh_render_profile profile)
{
	struct fixture *fixture;
	const struct mh_atlas *atlas;
	struct mh_image_collection *cutout, *opaque;
	unsigned int opens;

	fixture = source->context;
	fixture->opens = fixture->reads = fixture->closes = 0;
	init_scene(source, decoder, profile);
	cutout = opaque = NULL;
	CHECK(mh_image_collection_open(source, resources, "root",
		"Tracks/custom/Packed/tex1.pdi", &cutout,
		NULL) == MH_ERROR_NONE);

	if (profile == MH_RENDER_GLIDE)
		CHECK(mh_image_collection_open(source, resources, "root",
			"Tracks/custom/Packed/tex2.pdi", &opaque,
			NULL) == MH_ERROR_NONE);

	CHECK(mh_race_scene_register_textures(resources, "root", cutout, opaque,
		&scene, 0, NULL) == MH_ERROR_NONE);
	opens = profile == MH_RENDER_GLIDE ? 2 : 1;
	CHECK(fixture->opens == opens && fixture->closes == 0 &&
		fixture->reads == 2);
	atlas = &scene.assets.texture_atlas;
	CHECK(atlas->count == 2);
	CHECK(atlas->pages[0].rgba[20] ==
		(profile == MH_RENDER_GLIDE ? 80 : 41));
	CHECK(atlas->pages[0].rgba[23] == 255);
	CHECK(atlas->pages[1].rgba[23] == 0 && atlas->pages[1].rgba[27] == 255);
	CHECK(atlas->pages[1].rgba[20] ==
		(profile == MH_RENDER_POWERVR ? 34 : 41));
	CHECK(mh_race_scene_register_textures(resources, "root", cutout, opaque,
		&scene, 0, NULL) == MH_ERROR_NONE);
	CHECK(fixture->opens == opens && fixture->reads == 2);
	mh_image_collection_close(opaque);
	mh_image_collection_close(cutout);
	mh_race_scene_free(&scene);

	init_scene(source, decoder, profile);
	cutout = opaque = NULL;
	CHECK(mh_image_collection_open(source, resources, "root",
		"Tracks/custom/Packed/tex1.pdi", &cutout,
		NULL) == MH_ERROR_NONE);

	if (profile == MH_RENDER_GLIDE)
		CHECK(mh_image_collection_open(source, resources, "root",
			"Tracks/custom/Packed/tex2.pdi", &opaque,
			NULL) == MH_ERROR_NONE);

	fixture->reads = 0;
	fixture->fail_at = 2;
	CHECK(mh_race_scene_register_textures(resources, "root", cutout, opaque,
		&scene, 0, NULL) == MH_ERROR_IO);
	CHECK(scene.assets.texture_atlas.pages == NULL);
	mh_image_collection_close(opaque);
	mh_image_collection_close(cutout);
	mh_race_scene_free(&scene);
	CHECK(fixture->closes == fixture->opens);
	fixture->fail_at = 0;

	return 0;
}

static int
check_members(const struct mh_image_source_provider *source,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources)
{
	struct mh_image_collection *collection;
	struct mh_indexed_image image;
	struct mh_file file, before;

	CHECK(mh_image_collection_open(source, resources, "root",
		"Tracks/custom/Packed/tex1.pdi", &collection,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_image_collection_load_indexed(decoder, collection,
		"OPAQUE.IFF", &image, NULL) == MH_ERROR_NONE);
	CHECK(image.colors == 2 && image.indices[0] == 0 &&
		image.indices[1] == 1);
	CHECK(memcmp(image.palette, image.palette + 3, 3) == 0);
	free(image.indices);
	CHECK(mh_image_collection_read(collection, "cutout.iff", &file, NULL) ==
		MH_ERROR_NONE);
	before = file;
	CHECK(mh_image_collection_read(collection, "absent", &file, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(file.data == before.data && file.size == before.size);
	mh_image_collection_close(collection);
	CHECK(file.size == 1 && file.data[0] == 40);
	free(file.data);

	return 0;
}

int
main(void)
{
	static const struct mh_image_source_ops source_ops = {
		open_set, read_member, close_set};

	static const struct mh_image_decoder_ops decoder_ops = {
		read_rgba, read_indexed};

	struct mh_image_source_provider source;
	struct mh_image_decoder_provider decoder;
	struct mh_resource_entry entries[3];
	const struct mh_resource_source *resources;
	struct fixture fixture;
	unsigned char remap[REMAP_BYTES];
	char paths[3][64];
	unsigned int i;

	memset(&fixture, 0, sizeof(fixture));
	fixture.primary = 40;
	fixture.secondary = 80;
	source.ops = &source_ops;
	source.context = &fixture;
	decoder.ops = &decoder_ops;
	decoder.context = NULL;
	decoder.read = NULL;
	decoder.extension = "raw";
	make_remap(remap);

	for (i = 0; i < 3; i++) {
		sprintf(paths[i], "root/Tracks/custom/Packed/remap%u.bin", i);
		entries[i].path = paths[i];
		entries[i].data = remap;
		entries[i].size = sizeof(remap);
	}

	CHECK(mh_memory_resources_open(entries, 3, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(check_members(&source, &decoder, resources) == 0);
	CHECK(check_mesh(&source, &decoder, resources) == 0);

	for (i = MH_RENDER_GLIDE; i <= MH_RENDER_POWERVR; i++) {
		if (i != MH_RENDER_SOFTWARE &&
			mh_render_profile_available((enum mh_render_profile)i))
			CHECK(check_atlas(&source, &decoder, resources,
				(enum mh_render_profile)i) == 0);
	}

	mh_memory_resources_close(resources);
	puts("Selected image collections build meshes, indexed textures and runtime atlases without PDI files or original image encodings");

	return 0;
}
