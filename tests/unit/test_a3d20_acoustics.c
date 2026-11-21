#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/audio/a3d20_pcm.h"

#define RATE 8000
#define FRAMES 512
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "A3D acoustics line %d: %s\n",         \
				__LINE__, #test);                              \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_a3d20_pcm pcm;
static struct mh_a3d20_voice source;
static struct mh_a3d20_queue_sink sink;
static struct mh_audio_pcm wave;
static float samples[FRAMES * 2];
static unsigned char bytes[16];

static void
append_wall(struct mh_a3d20_geometry *scene, float x, float center_z)
{
	struct mh_a3d20_triangle *first, *second;
	unsigned int vertex;

	first = &scene->triangles[scene->count++];
	second = &scene->triangles[scene->count++];
	memset(first, 0, sizeof(*first));
	memset(second, 0, sizeof(*second));
	first->tag = scene->count * 2;
	second->tag = first->tag + 1;

	for (vertex = 0; vertex < 3; vertex++)
		first->vertices[vertex][0] = second->vertices[vertex][0] = x;

	first->vertices[0][1] = first->vertices[0][2] = -20;
	first->vertices[1][1] = first->vertices[2][1] = 20;
	first->vertices[1][2] = -20;
	first->vertices[2][2] = 20;
	memcpy(second->vertices[0], first->vertices[0],
		sizeof(first->vertices[0]));
	memcpy(second->vertices[1], first->vertices[2],
		sizeof(first->vertices[2]));
	second->vertices[2][1] = -20;
	second->vertices[2][2] = 20;

	for (vertex = 0; vertex < 3; vertex++) {
		first->vertices[vertex][2] += center_z;
		second->vertices[vertex][2] += center_z;
	}
}

static int
setup_source(int loop, int impulse)
{
	struct mh_a3d20_source_update position;
	struct mh_a3d20_playback_request play;

	mh_a3d20_pcm_destroy(&pcm);
	memset(&source, 0, sizeof(source));
	memset(&wave, 0, sizeof(wave));
	memset(bytes, impulse ? 128 : 192, sizeof(bytes));

	if (impulse)
		bytes[0] = 255;

	wave.pcm = bytes;
	wave.frames = wave.bytes = sizeof(bytes);
	wave.channels = 1;
	wave.bits = 8;
	wave.rate = RATE;
	source.sample_rate = RATE;
	source.input.near_distance = 1;
	source.input.far_distance = 100;
	CHECK(mh_a3d20_pcm_init(RATE, .5, &pcm, NULL) == MH_ERROR_NONE);
	CHECK(mh_a3d20_pcm_bind(&pcm, 0, &source, &wave, NULL) ==
		MH_ERROR_NONE);
	mh_a3d20_pcm_sink(&pcm, &sink);
	memset(&position, 0, sizeof(position));
	position.changed = 1;
	position.position[0] = 10;
	sink.source(sink.context, &source, &position);
	memset(&play, 0, sizeof(play));
	play.action = MH_A3D20_PLAYBACK_START;
	play.gain = play.pitch = 1;
	play.loop = loop;
	sink.playback(sink.context, &source, &play);

	return 0;
}

static int
render_frames(size_t count)
{
	CHECK(mh_a3d20_pcm_render(&pcm, samples, count, 0, NULL) ==
		MH_ERROR_NONE);

	return 0;
}

static double
stereo_energy(const float *data, size_t frames)
{
	double sum;
	size_t sample_index;

	sum = 0;

	for (sample_index = 0; sample_index < frames * 2; sample_index++)
		sum += data[sample_index] * data[sample_index];

	return sum;
}

static int
paths(void)
{
	struct mh_a3d20_paths paths;
	struct mh_spatial_pcm_voice *voice;
	double length;
	unsigned int i;

	CHECK(setup_source(1, 0) == 0);
	voice = &pcm.output.voices[0];
	append_wall(&pcm.geometry, 5, 0);
	CHECK(mh_a3d20_paths_build(&pcm.geometry, &pcm.material,
		&pcm.output.listener, voice, &paths,
		NULL) == MH_ERROR_NONE);
	CHECK(fabs(paths.transmission - .3) < .000001);
	CHECK(fabs(paths.transmission_hf - .3) < .000001 && paths.count == 0);

	/* The diagonal hit and a repeated cell reference are still one wall. */
	pcm.geometry.triangles[2] = pcm.geometry.triangles[0];
	pcm.geometry.count++;
	CHECK(mh_a3d20_paths_build(&pcm.geometry, &pcm.material,
		&pcm.output.listener, voice, &paths,
		NULL) == MH_ERROR_NONE);
	CHECK(fabs(paths.transmission - .3) < .000001);
	append_wall(&pcm.geometry, 7, 0);
	CHECK(mh_a3d20_paths_build(&pcm.geometry, &pcm.material,
		&pcm.output.listener, voice, &paths,
		NULL) == MH_ERROR_NONE);
	CHECK(fabs(paths.transmission - .09) < .000001);

	pcm.geometry.count = 0;
	append_wall(&pcm.geometry, 5, 100);
	CHECK(mh_a3d20_paths_build(&pcm.geometry, &pcm.material,
		&pcm.output.listener, voice, &paths,
		NULL) == MH_ERROR_NONE);
	CHECK(paths.transmission == 1 && paths.count == 0);

	pcm.geometry.count = 0;
	append_wall(&pcm.geometry, 5, 0);
	voice->source.position[0] = 0;
	voice->source.position[2] = 4;
	pcm.output.listener.position[2] = -4;
	CHECK(mh_a3d20_paths_build(&pcm.geometry, &pcm.material,
		&pcm.output.listener, voice, &paths,
		NULL) == MH_ERROR_NONE);
	length = sqrt(164.0);
	CHECK(paths.transmission == 1 && paths.count == 1);
	CHECK(fabs(paths.reflections[0].point[0] - 5) < .000001);
	CHECK(fabs(paths.reflections[0].gain - .96 * 8 / length) < .000001);
	CHECK(fabs(paths.reflections[0].delay - (length - 8) / 343.3 * .1) <
		.000001);
	CHECK(fabs(paths.reflections[0].pan - 5 / sqrt(41.0)) < .000001);

	/*
	 * A nearer opaque surface blocks both legs of the farther reflection.
	 */
	append_wall(&pcm.geometry, 2, 0);
	CHECK(mh_a3d20_paths_build(&pcm.geometry, &pcm.material,
		&pcm.output.listener, voice, &paths,
		NULL) == MH_ERROR_NONE);
	CHECK(paths.count == 1);

	for (i = 0; i < paths.count; i++)
		CHECK(fabs(paths.reflections[i].point[0] - 2) < .000001);

	return 0;
}

static int
occlusion(void)
{
	double dry, blocked, low, high;
	unsigned int i;

	CHECK(setup_source(1, 0) == 0);
	sink.flush(sink.context);
	CHECK(render_frames(FRAMES) == 0);
	dry = stereo_energy(samples, FRAMES);
	CHECK(dry > 0);
	append_wall(&pcm.geometry, 5, 0);
	sink.flush(sink.context);
	CHECK(render_frames(FRAMES) == 0);

	/* Let the native five-millisecond transition and filter settle. */
	CHECK(render_frames(FRAMES) == 0);
	blocked = stereo_energy(samples, FRAMES);
	CHECK(fabs(blocked / dry - .09) < .00001);
	pcm.material.transmittance[1] = 0;
	sink.flush(sink.context);
	CHECK(render_frames(FRAMES) == 0);
	CHECK(render_frames(FRAMES) == 0);
	CHECK(stereo_energy(samples, FRAMES) == 0);
	pcm.geometry.count = 0;
	sink.flush(sink.context);
	CHECK(render_frames(FRAMES) == 0);
	CHECK(render_frames(FRAMES) == 0);
	CHECK(fabs(stereo_energy(samples, FRAMES) / dry - 1) < .00001);

	/*
	 * Broadband gain held equal: the HF parameter suppresses a fast tone.
	 */
	CHECK(setup_source(1, 0) == 0);

	for (i = 0; i < sizeof(bytes); i++)
		bytes[i] = i % 2 ? 64 : 192;

	append_wall(&pcm.geometry, 5, 0);
	pcm.material.transmittance[0] = 1;
	pcm.material.transmittance[1] = 1;
	sink.flush(sink.context);
	CHECK(render_frames(FRAMES) == 0);
	high = stereo_energy(samples, FRAMES);
	pcm.material.transmittance[1] = .1f;
	sink.flush(sink.context);
	CHECK(render_frames(FRAMES) == 0);
	CHECK(render_frames(FRAMES) == 0);
	low = stereo_energy(samples, FRAMES);
	CHECK(low > 0 && low < high * .25);

	return 0;
}

static int
separate_sources(void)
{
	static struct mh_a3d20_voice other;
	struct mh_a3d20_source_update position;
	struct mh_a3d20_playback_request play;

	CHECK(setup_source(1, 0) == 0);

	/* The PCM binding retains this identity after clear. */
	other = source;
	CHECK(mh_a3d20_pcm_bind(&pcm, 1, &other, &wave, NULL) == MH_ERROR_NONE);
	memset(&position, 0, sizeof(position));
	position.changed = 1;
	position.position[0] = -10;
	sink.source(sink.context, &other, &position);
	memset(&play, 0, sizeof(play));
	play.action = MH_A3D20_PLAYBACK_START;
	play.gain = play.pitch = 1;
	play.loop = 1;
	sink.playback(sink.context, &other, &play);
	append_wall(&pcm.geometry, 5, 0);
	pcm.material.reflectance[0] = 0;
	sink.flush(sink.context);
	CHECK(render_frames(FRAMES) == 0);
	CHECK(fabs(samples[(FRAMES - 1) * 2] - .025) < .000001);
	CHECK(fabs(samples[(FRAMES - 1) * 2 + 1] - .0075) < .000001);
	mh_a3d20_pcm_clear(&pcm);

	return 0;
}

static int
reflection_setup(void)
{
	CHECK(setup_source(0, 1) == 0);

	/*
	 * Co-located source/listener, wall 10 m to the right: 20 m echo path.
	 */
	pcm.output.voices[0].source.position[0] = 0;
	append_wall(&pcm.geometry, 10, 0);
	pcm.material.reflectance[0] = pcm.material.reflectance[1] = 1;
	pcm.material.reflection_gain = pcm.material.reflection_delay = 1;
	sink.flush(sink.context);

	return 0;
}

static int
reflections(void)
{
	float whole[FRAMES * 2], chunks[FRAMES * 2];
	struct mh_a3d20_playback_status status;
	double expected, arrival;
	size_t first, i, offset, count;

	CHECK(reflection_setup() == 0);
	CHECK(render_frames(FRAMES) == 0);
	memcpy(whole, samples, sizeof(whole));
	arrival = 20.0 / 343.3 * RATE;
	first = (size_t)arrival;
	expected = (127.0 / 128) * .5 / 20;
	CHECK(whole[0] > 0 && whole[0] == whole[1]);

	for (i = 1; i < first; i++)
		CHECK(whole[i * 2] == 0 && whole[i * 2 + 1] == 0);

	CHECK(whole[first * 2] == 0);
	CHECK(fabs(whole[first * 2 + 1] - expected * (1 - (arrival - first))) <
		.000001);
	CHECK(fabs(whole[(first + 1) * 2 + 1] - expected * (arrival - first)) <
		.000001);
	sink.status(sink.context, &source, &status);
	CHECK(!status.playing);

	/*
	 * Callback partition and pause leave both delay time and PCM unchanged.
	 */
	CHECK(reflection_setup() == 0);
	offset = 0;

	while (offset < FRAMES) {
		count = FRAMES - offset < 7 ? FRAMES - offset : 7;
		CHECK(mh_a3d20_pcm_render(&pcm, samples, FRAMES, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(stereo_energy(samples, FRAMES) == 0);
		CHECK(mh_a3d20_pcm_render(&pcm, chunks + offset * 2, count, 0,
			NULL) == MH_ERROR_NONE);
		offset += count;
	}

	CHECK(memcmp(whole, chunks, sizeof(whole)) == 0);
	CHECK(render_frames(FRAMES) == 0 &&
		stereo_energy(samples, FRAMES) == 0);

	/* Global scales change echo gain and travel time independently. */
	CHECK(reflection_setup() == 0);
	pcm.material.reflection_gain = .5f;
	pcm.material.reflection_delay = .1f;
	sink.flush(sink.context);
	CHECK(render_frames(FRAMES) == 0);
	arrival *= (double)pcm.material.reflection_delay;
	first = (size_t)arrival;
	CHECK(fabs(samples[first * 2 + 1] -
		expected * .5 * (1 - (arrival - first))) < .000001);
	CHECK(samples[first * 2] == 0);

	CHECK(reflection_setup() == 0);
	pcm.material.reflectance[1] = 0;
	sink.flush(sink.context);
	CHECK(render_frames(FRAMES) == 0);
	CHECK(samples[0] > 0);
	CHECK(stereo_energy(samples + 2, FRAMES - 1) == 0);

	/*
	 * Effects volume controls already emitted echoes without stopping time.
	 */
	CHECK(reflection_setup() == 0);
	CHECK(render_frames(1) == 0);
	pcm.volume = 0;
	CHECK(render_frames(FRAMES) == 0 &&
		stereo_energy(samples, FRAMES) == 0);
	pcm.volume = 1;
	CHECK(render_frames(FRAMES) == 0 &&
		stereo_energy(samples, FRAMES) == 0);

	/*
	 * Explicit reset discards travelling echoes; source completion did not.
	 */
	CHECK(reflection_setup() == 0);
	CHECK(render_frames(1) == 0);
	mh_a3d20_pcm_clear(&pcm);
	CHECK(render_frames(FRAMES) == 0 &&
		stereo_energy(samples, FRAMES) == 0);

	return 0;
}

int
main(void)
{
	int status;

	status = paths() || occlusion() || separate_sources() || reflections();
	mh_a3d20_pcm_destroy(&pcm);

	if (status == 0)
		puts("A3D acoustics: wall transmission/HF, reflection geometry/delay/direction, pause/reset and partition checks pass");

	return status;
}
