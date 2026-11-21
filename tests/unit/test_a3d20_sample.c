#include <stdio.h>
#include <string.h>

#include "engine/audio/a3d20_sample.h"
#include "engine/core/binary.h"

#define CATALOG_SIZE 2

static const char *const catalogs[][CATALOG_SIZE] = {
	{"fixture.wav", "fixture.wav"}, {"FIXTURE.wav", "fixture.wav"},
	{"fixture.wav.bak", "other.wav"}, {"other.wav", "FIXTURE.wav"},
	{"other.wav", "fixture.wav"}};

static int
check_plans(void)
{
	struct mh_a3d20_sample_input input;
	struct mh_a3d20_sample_plan plan;
	struct mh_audio_pcm wave;
	struct mh_error error;
	unsigned char pcm[2048], saved[sizeof(plan)];

	memset(&plan, 0, sizeof(plan));
	wave.pcm = pcm;
	wave.bytes = sizeof(pcm);
	wave.bits = 16;
	wave.channels = 2;
	wave.rate = 22050;
	wave.frames = 512;
	input.name = "fixture.wav";
	input.fade_in_samples = 101;
	input.fade_out_samples = 200;
	input.near_distance = -2;
	input.far_distance = 0;

	if (mh_a3d20_sample_prepare(&wave, &input, NULL, 0, &plan, &error) !=
			MH_ERROR_NONE ||
		error.message != NULL || plan.duplicate_index != 0 ||
		!plan.buffer.spatial || plan.buffer.software_only ||
		plan.buffer.offset != 202 || plan.buffer.bytes != 1446 ||
		plan.buffer.fade_in_samples != 101 ||
		plan.buffer.fade_out_samples != 200 ||
		plan.buffer.near_distance != 1 ||
		plan.buffer.far_distance != (float)1.1)
		return 0;

	/*
	 * Duplicate selection is case sensitive and stops at the newest match.
	 */
	if (mh_a3d20_sample_prepare(&wave, &input, catalogs[0], CATALOG_SIZE,
		&plan, NULL) != MH_ERROR_NONE ||
		plan.duplicate_index != 0)
		return 0;

	if (mh_a3d20_sample_prepare(&wave, &input, catalogs[1], CATALOG_SIZE,
		&plan, NULL) != MH_ERROR_NONE ||
		plan.duplicate_index != 1)
		return 0;

	if (mh_a3d20_sample_prepare(&wave, &input, catalogs[2], CATALOG_SIZE,
		&plan, NULL) != MH_ERROR_NONE ||
		plan.duplicate_index != CATALOG_SIZE)
		return 0;

	/* Min/max are clamped independently; preserve reversed distances. */
	input.near_distance = 40;
	input.far_distance = 2;
	input.fade_in_samples = input.fade_out_samples = 512;

	if (mh_a3d20_sample_prepare(&wave, &input, catalogs[0], CATALOG_SIZE,
		&plan, NULL) != MH_ERROR_NONE ||
		plan.buffer.offset != 0 || plan.buffer.bytes != sizeof(pcm) ||
		plan.buffer.fade_in_samples != 0 ||
		plan.buffer.fade_out_samples != 0 ||
		plan.buffer.near_distance != 40 ||
		plan.buffer.far_distance != 2)
		return 0;

	/* Even duplicates reject overlapping cuts before changing the plan. */
	memcpy(saved, &plan, sizeof(saved));
	input.fade_out_samples++;

	if (mh_a3d20_sample_prepare(&wave, &input, catalogs[0], CATALOG_SIZE,
		&plan, &error) != MH_ERROR_RANGE ||
		error.message == NULL ||
		memcmp(saved, &plan, sizeof(saved)) != 0)
		return 0;

	return mh_a3d20_sample_prepare(&wave, &input, NULL, CATALOG_SIZE, &plan,
		NULL) == MH_ERROR_INVALID;
}

int
main(void)
{

	if (!check_plans()) {
		fputs("A3D 2.0 sample preparation failed\n", stderr);
		return 1;
	}

	puts("A3D 2.0 sample ranges, distances and duplicate selection passed");

	return 0;
}
