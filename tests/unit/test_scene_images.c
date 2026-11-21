#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/catalog/halo_paths.h"
#include "engine/render/atmosphere_sky.h"
#include "engine/render/scene_internal.h"
#include "interfaces/image/decoder_backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "scene images:%d: %s\n", __LINE__,     \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	RESOURCE_COUNT = 103,
	PATH_BYTES = 128
};

struct fixture {
	struct mh_resource_entry	entries[RESOURCE_COUNT];
	char				paths[RESOURCE_COUNT][PATH_BYTES];
	unsigned char			bytes[RESOURCE_COUNT][4];
	unsigned int	count, rgba_calls, indexed_calls, shared_calls, fail_at;
	unsigned int			rgba_fail_at;
	int				true_color;
};

static struct fixture fixture;
static struct mh_race_scene scene;

static void
add_image(const char *path, unsigned char kind, unsigned int size,
	unsigned int group, unsigned int frame)
{
	unsigned int i;

	i = fixture.count++;
	strcpy(fixture.paths[i], path);
	fixture.bytes[i][0] = kind;
	fixture.bytes[i][1] = (unsigned char)size;
	fixture.bytes[i][2] = (unsigned char)group;
	fixture.bytes[i][3] = (unsigned char)frame;
	fixture.entries[i].path = fixture.paths[i];
	fixture.entries[i].data = fixture.bytes[i];
	fixture.entries[i].size = 4;
}

static void
add_frames(const char *pattern, unsigned int first, unsigned int count,
	unsigned int size, unsigned char kind, unsigned int group)
{
	char path[PATH_BYTES];
	unsigned int frame;

	for (frame = 0; frame < count; frame++) {
		sprintf(path, pattern, first + frame);
		add_image(path, kind, size, group, frame);
	}
}

static void
make_resources(void)
{
	add_frames("root/Data/smoke/smoke%02u.raw", 21, 17, 64, 'S', 0);
	add_frames("root/Data/gras/_cgras%02u.raw", 40, 17, 64, 'S', 1);
	add_frames("root/Data/grus/_grus%02u.raw", 20, 17, 64, 'S', 2);
	add_frames("root/Data/sparks3/_spare%02u.raw", 20, 20, 128, 'S', 3);
	add_frames("root/Data/d3d/smoke/smoke%02u.raw", 1, 9, 32, 'G', 0);
	add_frames("root/Data/d3d/grass/gras%02u.raw", 1, 9, 32, 'G', 1);
	add_frames("root/Data/d3d/sparks/spark%02u.raw", 1, 6, 64, 'G', 2);
	add_image("root/Data/halofix.raw", 'H', 128, 0, 0);
	add_image("root/Data/hwhex.raw", 'H', 64, 1, 1);
	add_image("root/Data/hex.raw", 'S', 2, 0, 0);
	add_image("root/Data/glow.raw", 'S', 2, 0, 0);
	add_image("root/Data/sky.raw", 'K', 2, 0, 0);
	add_image("root/Data/env1.raw", 'S', 2, 0, 0);
	add_image("root/Data/env2.raw", 'S', 2, 0, 1);
	add_image("root/Data/env3.raw", 'S', 2, 0, 2);
}

static enum mh_error_code
raw_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette, struct mh_error *error_out)
{
	struct fixture *test;
	struct mh_image image;
	unsigned int color;
	int shared;

	test = context;
	test->rgba_calls++;

	if (test->rgba_calls == test->rgba_fail_at)
		return mh_error_set(
			error_out, MH_ERROR_IO, "test RGBA failure");

	if (size != 4 ||
		(data[0] != 'G' && data[0] != 'K' && data[0] != 'H' &&
			data[0] != 'S') ||
		options->alpha != MH_IMAGE_ALPHA_FILE)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test RGBA request");

	color = data[0] == 'K' ? 77 : 40 + data[2];

	if (data[0] == 'S')
		color = 73 + data[3] * 43;

	shared = data[0] == 'G' && data[3] != 0 && !test->true_color;

	if (shared != (options->palette != NULL))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test palette selection");

	if (shared) {
		test->shared_calls++;

		if (options->palette->count != 2 ||
			options->palette->rgb[0] != color)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"test shared palette");
	}

	image.width = image.height = data[1];
	image.rgba = malloc((size_t)image.width * image.height * 4);

	if (image.rgba == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test RGBA allocation");

	memset(image.rgba, (int)color, (size_t)image.width * image.height * 4);
	*output = image;

	if (palette != NULL) {
		memset(palette, 0, sizeof(*palette));

		if (!test->true_color) {
			palette->count = 2;
			memset(palette->rgb, (int)color, 6);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
raw_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	struct fixture *test;
	struct mh_indexed_image image;
	size_t i;

	test = context;
	test->indexed_calls++;

	if (test->indexed_calls == test->fail_at)
		return mh_error_set(
			error_out, MH_ERROR_IO, "test frame read failure");

	if (size != 4 || (data[0] != 'S' && data[0] != 'H'))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test indexed request");

	memset(&image, 0, sizeof(image));
	image.width = image.height = data[1];
	image.size = (size_t)image.width * image.height;
	image.colors = 2;
	memset(image.palette, 10 + data[3], 6);
	image.indices = malloc(image.size);

	if (image.indices == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test index allocation");

	for (i = 0; i < image.size; i++)
		image.indices[i] = (unsigned char)(i % 2);

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_software(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources)
{
	struct mh_software_scene_assets *assets;
	const struct mh_indexed_image *image;
	struct mh_software_particle_assets particles, before;
	unsigned int group, frame;

	scene.image_decoder = decoder;
	scene.assets.profile = MH_RENDER_SOFTWARE;
	strcpy(scene.assets.track.background, "Data/sky.tga");
	strcpy(scene.assets.track.environment[0], "Data/env1.iff");
	strcpy(scene.assets.track.environment[1], "Data/env2.iff");
	strcpy(scene.assets.track.environment[2], "Data/env3.iff");
	CHECK(mh_halo_paths_append(&scene.assets.track.halos, "Data/glow.iff",
		NULL) == MH_ERROR_NONE);
	CHECK(mh_halo_paths_append(&scene.assets.track.halos, "Data/glow.iff",
		NULL) == MH_ERROR_NONE);
	CHECK(mh_race_scene_load_software(resources, "root", &scene, NULL) ==
		MH_ERROR_NONE);
	assets = &scene.assets.software;
	CHECK(fixture.rgba_calls == 1 && fixture.indexed_calls == 77);
	CHECK(assets->sky.width == 2 && assets->sky.rgba[0] == 77);
	CHECK(assets->lens.indices[1] == 1);
	CHECK(assets->track_halos.count == 2);
	CHECK(assets->track_halos.images[0].indices !=
		assets->track_halos.images[1].indices);

	for (group = 0; group < MH_PARTICLE_ANIMATIONS; group++) {
		CHECK(assets->particles.animations[group].count ==
			(group == 3 ? 20 : 17));

		for (frame = 0;
			frame < assets->particles.animations[group].count;
			frame++) {
			image = &assets->particles.animations[group]
					.frames[frame];
			CHECK(image->width == (group == 3 ? 128 : 64));
			CHECK(image->indices[0] == 0 && image->indices[1] == 1);
			CHECK(image->colors == 2 &&
				image->palette[0] == 10 + frame);
			CHECK(memcmp(image->palette, image->palette + 3, 3) ==
				0);
		}
	}

	mh_race_scene_free(&scene);
	memset(&before, 0, sizeof(before));
	before.animations[0].count = 99;
	particles = before;
	fixture.indexed_calls = 0;
	fixture.fail_at = 5;
	CHECK(mh_software_particle_assets_load_from(decoder, resources, "root",
		&particles, NULL) == MH_ERROR_IO);
	CHECK(memcmp(&particles, &before, sizeof(particles)) == 0);
	fixture.fail_at = 0;

	return 0;
}

static int
check_sky(const struct mh_image_decoder_provider *decoder)
{
	const unsigned char data[4] = {'K', 2, 0, 0};
	struct mh_mesh mesh;
	unsigned int i;

	CHECK(mh_sky_mesh_create(decoder, data, sizeof(data), &mesh, NULL) ==
		MH_ERROR_NONE);
	CHECK(mesh.textures[1].width == 256 && mesh.textures[1].height == 2);

	for (i = 0; i < 256 * 2 * 4; i++)
		CHECK(mesh.textures[1].rgba[i] == 77);

	mh_mesh_free(&mesh);

	return 0;
}

static int
check_hardware(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources,
	enum mh_render_profile profile, int true_color)
{
	struct mh_mesh *mesh;
	unsigned int x, y;
	float uv[8];
	size_t offset;

	fixture.rgba_calls = fixture.shared_calls = 0;
	fixture.true_color = true_color;
	scene.assets.profile = profile;
	CHECK(mh_race_scene_load_particles_from(decoder, resources, "root",
		&scene, NULL) == MH_ERROR_NONE);
	CHECK(fixture.rgba_calls == 24);
	CHECK(fixture.shared_calls == (true_color ? 0 : 21));
	mesh = &scene.visuals.particle_mesh;
	CHECK(mh_particle_frame_uv(MH_PARTICLE_SMOKE, 0, uv, NULL) ==
		MH_ERROR_NONE);
	x = (unsigned int)(uv[0] * MH_PARTICLE_ATLAS_SIZE);
	y = (unsigned int)(uv[1] * MH_PARTICLE_ATLAS_SIZE);
	offset = ((size_t)y * MH_PARTICLE_ATLAS_SIZE + x) * 4;
	CHECK(mesh->textures[0].rgba[offset] == 40);
	CHECK(mesh->textures[0].rgba[offset + 3] ==
		(profile == MH_RENDER_POWERVR ? 40 : 255));

	if (profile == MH_RENDER_POWERVR)
		CHECK(mesh->texture_formats[0] == MH_TEXTURE_ARGB4444);

	mh_race_scene_free(&scene);

	return 0;
}

static int
check_lamp_atlas(void)
{
	const struct mh_lamps *lamps;
	const struct mh_image *atlas;
	unsigned int pixel;

	lamps = &scene.visuals.lamps;
	CHECK(fixture.indexed_calls == 2);
	CHECK(scene.assets.lamp_atlas.count == 1);
	CHECK(scene.assets.lamp_atlas.formats[0] == MH_TEXTURE_ARGB4444);
	CHECK(scene.assets.lamp_palette[0] == 10);
	atlas = &scene.assets.lamp_atlas.pages[0];
	CHECK(atlas->width == 256 && atlas->height == 256);

	/*
	 * Both indices have the same RGB; flare uses the glow palette.
	 */
	for (pixel = 0; pixel < 128; pixel++) {
		CHECK(atlas->rgba[pixel * 4] == 255);
		CHECK(atlas->rgba[pixel * 4 + 3] == 10);
	}

	for (pixel = 128; pixel < 192; pixel++) {
		CHECK(atlas->rgba[pixel * 4] == 255);
		CHECK(atlas->rgba[pixel * 4 + 3] == 30);
	}

	CHECK(lamps->mesh.atlas == &scene.assets.lamp_atlas);
	CHECK(lamps->mesh.textures[0].rgba == NULL);
	CHECK(scene.visuals.shadow_mesh.atlas == &scene.assets.lamp_atlas);

	return 0;
}

static int
check_lamps(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources,
	enum mh_render_profile profile)
{
	struct mh_lamps *lamps;

	fixture.rgba_calls = fixture.indexed_calls = 0;
	scene.image_decoder = decoder;
	scene.assets.profile = profile;
	CHECK(mh_shadow_mesh_create(&scene.visuals.shadow_mesh, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_scene_load_lamps_from(resources, "root", &scene, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture.rgba_calls == 2);
	lamps = &scene.visuals.lamps;
	CHECK(lamps->mesh.texture_count == 2);
	CHECK(lamps->visible[0] == 1);

	if (profile == MH_RENDER_POWERVR) {
		CHECK(check_lamp_atlas() == 0);
	} else {
		CHECK(fixture.indexed_calls == 0);
		CHECK(lamps->mesh.textures[0].width == 128);
		CHECK(lamps->mesh.textures[1].width == 64);
		CHECK(lamps->mesh.textures[0].rgba[0] == 40);
		CHECK(lamps->mesh.textures[0].rgba[3] == 40);
		CHECK(lamps->mesh.textures[1].rgba[0] == 41);
	}

	mh_race_scene_free(&scene);
	fixture.rgba_calls = 0;
	fixture.rgba_fail_at = 2;
	scene.image_decoder = decoder;
	scene.assets.profile = profile;
	CHECK(mh_race_scene_load_lamps_from(resources, "root", &scene, NULL) ==
		MH_ERROR_IO);
	CHECK(scene.visuals.lamps.mesh.textures == NULL);
	mh_race_scene_free(&scene);
	fixture.rgba_fail_at = 0;

	if (profile == MH_RENDER_POWERVR) {
		fixture.indexed_calls = 0;
		fixture.fail_at = 2;
		scene.image_decoder = decoder;
		scene.assets.profile = profile;
		CHECK(mh_shadow_mesh_create(&scene.visuals.shadow_mesh, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_race_scene_load_lamps_from(
			resources, "root", &scene, NULL) == MH_ERROR_IO);

		/*
		 * Stage failure retains partial allocations for scene teardown.
		 */
		CHECK(scene.visuals.lamps.mesh.textures[0].rgba != NULL);
		mh_race_scene_free(&scene);
		fixture.fail_at = 0;
	}

	return 0;
}

static int
check_environment(const struct mh_image_decoder_provider *decoder)
{
	const unsigned char data[4] = {'K', 2, 0, 0};
	struct mh_mesh mesh;
	unsigned char *retained;

	CHECK(mh_shadow_mesh_create(&mesh, NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_set_environment(decoder, &mesh, data, sizeof(data),
		MH_TEXTURE_RGB565, NULL) == MH_ERROR_NONE);
	CHECK(mesh.texture_count == 2 && mesh.environment_texture == 1);
	CHECK(mesh.textures[1].rgba[0] == 74 && mesh.textures[1].rgba[1] == 77);
	CHECK(mesh.textures[1].rgba[2] == 74 &&
		mesh.textures[1].rgba[3] == 255);
	CHECK(mesh.texture_formats[1] == MH_TEXTURE_RGB565);
	retained = mesh.textures[1].rgba;
	CHECK(mh_mesh_set_environment(decoder, &mesh, data, 3,
		MH_TEXTURE_RGB565, NULL) == MH_ERROR_INVALID);
	CHECK(mesh.textures[1].rgba == retained && mesh.texture_count == 2);
	CHECK(mh_mesh_set_environment(decoder, &mesh, data, sizeof(data),
		MH_TEXTURE_XRGB8888, NULL) == MH_ERROR_NONE);
	CHECK(mesh.texture_count == 2 && mesh.environment_texture == 1);
	CHECK(mesh.textures[1].rgba[0] == 77 &&
		mesh.textures[1].rgba[3] == 255);
	CHECK(mesh.texture_formats[1] == MH_TEXTURE_XRGB8888);
	mh_mesh_free(&mesh);

	return 0;
}

static int
check_effect_images(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources,
	enum mh_render_profile profile)
{
	const struct mh_mesh *mesh;

	fixture.rgba_calls = fixture.indexed_calls = 0;
	scene.image_decoder = decoder;
	scene.assets.profile = profile;
	scene.assets.opaque_format = MH_TEXTURE_RGB555;
	strcpy(scene.assets.track.background, "Data/sky.tga");
	strcpy(scene.assets.track.environment[0], "Data/env1.iff");
	CHECK(mh_race_scene_load_effects(resources, "root", &scene, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture.rgba_calls == 28);
	mesh = &scene.visuals.sky_mesh;
	CHECK(mesh->environment_texture == 2 && mesh->texture_count == 3);
	CHECK(mesh->textures[2].rgba[0] == 74 &&
		mesh->textures[2].rgba[3] == 255);
	CHECK(mesh->texture_formats[2] == MH_TEXTURE_RGB555);
	CHECK(mesh->textures[1].rgba[0] ==
		(profile == MH_RENDER_GLIDE ? 77 : 74));
	mh_race_scene_free(&scene);

	return 0;
}

static int
check_glenz(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources,
	enum mh_render_profile profile)
{
	struct mh_mesh *mesh;
	unsigned char *reflection;

	fixture.rgba_calls = 0;
	scene.image_decoder = decoder;
	scene.assets.profile = profile;
	scene.assets.opaque_format = MH_TEXTURE_RGB565;
	strcpy(scene.assets.track.environment[1], "Data/env2.iff");
	strcpy(scene.assets.track.environment[2], "Data/env3.iff");
	mesh = &scene.assets.meshes[1];
	CHECK(mh_shadow_mesh_create(mesh, NULL) == MH_ERROR_NONE);
	mesh->atlas = &scene.assets.texture_atlas;
	mesh->atlas_images = malloc(sizeof(*mesh->atlas_images));
	CHECK(mesh->atlas_images != NULL);
	mesh->atlas_images[0] = 7;
	scene.visuals.glenz.scroll[0].phase = 0.5f;
	CHECK(mh_race_scene_load_glenz(resources, "root", &scene, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture.rgba_calls == 2 && mesh->texture_count == 3);
	CHECK(scene.visuals.glenz.textures[1][0] == 1);
	CHECK(scene.visuals.glenz.textures[1][1] == 2);
	CHECK(mesh->textures[1].rgba[0] == 115 &&
		mesh->textures[1].rgba[1] == 117);
	CHECK(mesh->textures[2].rgba[0] == 156 &&
		mesh->textures[2].rgba[1] == 158);
	CHECK(mesh->textures[1].rgba[3] == 255);
	CHECK(mesh->atlas_images[0] == 7);
	CHECK(mesh->atlas_images[1] == (size_t)-1 &&
		mesh->atlas_images[2] == (size_t)-1);
	reflection = mesh->textures[1].rgba;
	CHECK(mh_race_scene_load_glenz(resources, "root", &scene, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture.rgba_calls == 2 && mesh->texture_count == 3);
	CHECK(mesh->textures[1].rgba == reflection);
	CHECK(scene.visuals.glenz.scroll[0].phase == 0.5f);
	mh_race_scene_free(&scene);

	return 0;
}

int
main(void)
{
	static const struct mh_image_decoder_ops ops = {raw_rgba, raw_indexed};
	struct mh_image_decoder_provider decoder;
	const struct mh_resource_source *resources;
	unsigned int profile;

	make_resources();
	CHECK(fixture.count == RESOURCE_COUNT);
	CHECK(mh_memory_resources_open(fixture.entries, fixture.count,
		&resources, NULL) == MH_ERROR_NONE);
	decoder.ops = &ops;
	decoder.context = &fixture;
	decoder.read = NULL;
	decoder.extension = "raw";
	CHECK(check_software(&decoder, resources) == 0);
	CHECK(check_sky(&decoder) == 0);
	CHECK(check_environment(&decoder) == 0);

	for (profile = MH_RENDER_GLIDE; profile <= MH_RENDER_POWERVR;
		profile++) {
		if (profile == MH_RENDER_SOFTWARE ||
			!mh_render_profile_available(
				(enum mh_render_profile)profile))
			continue;

		CHECK(check_hardware(&decoder, resources,
			(enum mh_render_profile)profile, 0) == 0);
		CHECK(check_hardware(&decoder, resources,
			(enum mh_render_profile)profile, 1) == 0);
		CHECK(check_lamps(&decoder, resources,
			(enum mh_render_profile)profile) == 0);
		CHECK(check_effect_images(&decoder, resources,
			(enum mh_render_profile)profile) == 0);

		if (profile != MH_RENDER_POWERVR)
			CHECK(check_glenz(&decoder, resources,
				(enum mh_render_profile)profile) == 0);
	}

	mh_memory_resources_close(resources);
	puts("Selected scene images preserve software indices, hardware shared palettes and converted true-color particles");

	return 0;
}
