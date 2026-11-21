#include <stdio.h>
#include <string.h>

#include "engine/audio/spatial_buffer.h"
#include "engine/core/binary.h"

static int
examples(void)
{
	struct mh_spatial_buffer_input input;
	struct mh_spatial_buffer_plan plan;
	struct mh_audio_pcm wave;
	struct mh_error error;
	unsigned char pcm[2048];
	unsigned char saved[sizeof(plan)];

	memset(&input, 0, sizeof(input));
	memset(&plan, 0, sizeof(plan));
	wave.pcm = pcm;
	wave.bytes = sizeof(pcm);
	wave.bits = 16;
	wave.channels = 2;
	wave.rate = 22050;
	wave.frames = 512;
	input.spatial = 1;
	input.fade_in_samples = 101;
	input.fade_out_samples = 200;
	input.near_distance = -2;
	input.far_distance = 40;

	if (mh_spatial_buffer_prepare(&wave, &input, &plan, &error) !=
			MH_ERROR_NONE ||
		error.message != NULL || !plan.spatial || plan.offset != 202 ||
		plan.bytes != 1446 || plan.fade_in_samples != 101 ||
		plan.fade_out_samples != 200 || plan.near_distance != 1 ||
		plan.far_distance != 40 || plan.software_only)
		return 0;

	/* Panned buffers do not clamp or apply spatial distances. */
	input.spatial = 0;
	input.fade_in_samples = 99;
	input.fade_out_samples = 0;

	if (mh_spatial_buffer_prepare(&wave, &input, &plan, NULL) !=
			MH_ERROR_NONE ||
		plan.spatial || plan.offset != 0 || plan.bytes != sizeof(pcm) ||
		plan.fade_in_samples != 0 || plan.near_distance != -2 ||
		plan.software_only)
		return 0;

	/*
	 * Software placement is an explicit caller choice.
	 */
	input.software_only = 1;

	if (mh_spatial_buffer_prepare(&wave, &input, &plan, NULL) !=
			MH_ERROR_NONE ||
		plan.spatial || !plan.software_only || plan.near_distance != -2)
		return 0;

	input.spatial = 1;
	input.software_only = 0;

	if (mh_spatial_buffer_prepare(&wave, &input, &plan, NULL) !=
			MH_ERROR_NONE ||
		!plan.spatial || plan.software_only || plan.near_distance != 1)
		return 0;

	input.fade_in_samples = 512;
	input.fade_out_samples = 512;

	if (mh_spatial_buffer_prepare(&wave, &input, &plan, NULL) !=
			MH_ERROR_NONE ||
		plan.offset != 0 || plan.bytes != sizeof(pcm) ||
		plan.fade_in_samples != 0 || plan.fade_out_samples != 0)
		return 0;

	memcpy(saved, &plan, sizeof(saved));
	input.fade_out_samples++;

	if (mh_spatial_buffer_prepare(&wave, &input, &plan, &error) !=
			MH_ERROR_RANGE ||
		error.message == NULL ||
		memcmp(saved, &plan, sizeof(saved)) != 0)
		return 0;

	return mh_spatial_buffer_prepare(NULL, &input, &plan, NULL) ==
		MH_ERROR_INVALID;
}

int
main(void)
{

	if (!examples()) {
		fputs("spatial buffer preparation failed\n", stderr);
		return 1;
	}

	puts("spatial buffer ranges and placement passed");

	return 0;
}
