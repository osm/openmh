#include <math.h>
#include <string.h>

#include "engine/audio/surface_internal.h"
#include "engine/core/error.h"

#define SAMPLE_FRAMES_MAX 32000000UL
#define WHEEL_MATERIAL_MAX 25
#define BODY_HARD_IMPULSE 16500

static int
boolean_valid(int value)
{
	return value == 0 || value == 1;
}

enum mh_error_code
mh_surface_slip_parameters(const struct mh_vehicle_state *vehicle, int *active,
	double *gain, struct mh_error *error_out)
{
	float amount;

	if (!boolean_valid(vehicle->grip.sliding) ||
		!boolean_valid(vehicle->wheelspin) ||
		!boolean_valid(vehicle->heavy_brake) ||
		!(vehicle->grip.slide_amount >= -1e12 &&
			vehicle->grip.slide_amount <= 1e12))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid surface sound slip");

	*active = vehicle->grip.sliding && !vehicle->wheelspin &&
		!vehicle->heavy_brake;

	if (*active) {
		amount = (float)vehicle->grip.slide_amount;
		*gain = (double)(float)(((long double)amount * .15 + .1) *
			(float).7);
	} else
		*gain = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
select_material(const struct mh_materials *materials,
	const struct mh_vehicle_state *vehicle, int *output,
	struct mh_error *error_out)
{
	const struct mh_material_sound *candidate, *selected;
	unsigned long material_id;
	unsigned int wheel;
	int material;

	selected = NULL;
	material = -1;

	for (wheel = 0; wheel < 4; wheel++) {
		if (!boolean_valid(vehicle->grip.wheels[wheel].supported))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid sound wheel support");

		if (!vehicle->grip.wheels[wheel].supported)
			continue;

		material_id = vehicle->grip.wheels[wheel].material;

		if (material_id > WHEEL_MATERIAL_MAX)
			material_id = 0;

		if (material_id >= materials->count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid sound wheel material");

		candidate = &materials->entries[material_id]
				.sounds[MH_MATERIAL_CONTACT];

		if (selected == NULL ||
			candidate->priority > selected->priority) {
			material = (int)material_id;
			selected = candidate;
		}
	}

	*output = material;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_surface_parameters(const struct mh_materials *materials,
	const struct mh_vehicle_state *vehicle, int listener_mode,
	struct mh_surface_parameters *output, struct mh_error *error_out)
{
	struct mh_surface_parameters params;
	const struct mh_material *material;
	long double gain;
	double amount, speed, contact, spin_volume, slide_scale;
	enum mh_error_code status;

	if (materials == NULL || materials->count == 0 ||
		materials->count > MH_MATERIAL_LIMIT || vehicle == NULL ||
		output == NULL || !boolean_valid(listener_mode) ||
		!(vehicle->motion.velocity[2] >= -1e12 &&
			vehicle->motion.velocity[2] <= 1e12))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid surface sound input");

	memset(&params, 0, sizeof(params));
	params.material = -1;
	status = mh_surface_slip_parameters(
		vehicle, &params.remote_slide, &params.remote_gain, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = select_material(
		materials, vehicle, &params.material, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (params.material >= 0) {
		material = &materials->entries[params.material];
		contact = material->sounds[MH_MATERIAL_CONTACT].volume;
		spin_volume = material->sounds[MH_MATERIAL_SPIN].volume;

		if (!(contact >= 0 && contact <= 100) ||
			!(spin_volume >= 0 && spin_volume <= 100))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid material sound volume");

		amount = (double)(float)vehicle->grip.slide_amount;
		params.spin = vehicle->wheelspin || vehicle->heavy_brake;

		if (params.spin) {
			gain = (long double)amount * .3 * (float).4 *
				(float)spin_volume;
			params.spin_gain = gain > 1 ? 1 : (double)(float)gain;
			params.spin_pitch =
				vehicle->heavy_brake ? (double)(float)1.08 : 1;
		}

		if (params.remote_slide) {
			params.slide = material->slide_type == 1
				? MH_SURFACE_SLIDE_GLOBAL
				: MH_SURFACE_SLIDE_MATERIAL;
			slide_scale = params.slide == MH_SURFACE_SLIDE_GLOBAL
				? .35
				: .035;
			params.slide_gain =
				(double)(float)(((long double)amount *
								slide_scale +
							.3) *
					(float).7);
		}

		speed = fabs((double)(float)vehicle->motion.velocity[2]);
		gain = (long double)(float).2 * speed *
			(listener_mode ? 1 : (double)(float)1.2) * .1;
		params.contact_gain = gain > 1 ? 1 : (double)(float)gain;
		gain = (long double)(float)contact * (float)params.contact_gain;
		params.contact_gain = gain > 1 ? 1 : (double)(float)gain;
	}

	*output = params;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_surface_loop(const struct mh_audio_pcm *wave, unsigned long start,
	unsigned long tail, size_t *loop_start, size_t *loop_end,
	struct mh_error *error_out)
{
	size_t first, end, swap;

	if (wave == NULL || wave->channels != 1 || wave->bits != 16 ||
		wave->frames == 0 || wave->frames > SAMPLE_FRAMES_MAX ||
		start > SAMPLE_FRAMES_MAX || tail > SAMPLE_FRAMES_MAX ||
		loop_start == NULL || loop_end == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid surface sound loop");

	first = start > wave->frames ? wave->frames : (size_t)start;
	end = tail > wave->frames ? 0 : wave->frames - (size_t)tail;

	if (first > end) {
		swap = first;
		first = end;
		end = swap;
	}

	if (first == end) {
		first = 0;
		end = wave->frames;
	}

	*loop_start = first;
	*loop_end = end;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_surface_body_gains(const struct mh_materials *materials, int contact,
	unsigned long material, double published_impulse,
	const struct mh_body_sparks *sparks,
	struct mh_body_sound_parameters *output, struct mh_error *error_out)
{
	struct mh_body_sound_parameters params;
	const struct mh_material *definition;
	double impulse, weak, hard, scratch;

	if (output == NULL || materials == NULL || materials->count == 0 ||
		materials->count > MH_MATERIAL_LIMIT || sparks == NULL ||
		(contact != 0 && contact != 1) ||
		!(published_impulse >= 0 && published_impulse <= 1e15) ||
		!(sparks->gain[0] >= -1e12 && sparks->gain[0] <= 1e12) ||
		sparks->corner[0] > 6)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid body sound publication");

	memset(&params, 0, sizeof(params));
	params.material = -1;

	if (contact) {
		if (material >= materials->count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid body sound material");

		params.material = (int)material;
		definition = &materials->entries[params.material];
		weak = definition->sounds[MH_MATERIAL_WEAK].volume;
		hard = definition->sounds[MH_MATERIAL_HARD].volume;
		scratch = definition->sounds[MH_MATERIAL_SCRATCH].volume;

		if (!(weak >= 0 && weak <= 100) ||
			!(hard >= 0 && hard <= 100) ||
			!(scratch >= 0 && scratch <= 100))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid body sound volume");

		impulse = (double)(float)published_impulse;
		params.weak = impulse > 0 && impulse < BODY_HARD_IMPULSE;
		params.hard = impulse >= BODY_HARD_IMPULSE;

		if (params.weak)
			params.weak_gain =
				(double)(float)((long double)impulse *
					(1.0 / BODY_HARD_IMPULSE) *
					(float)weak * (float).7);

		if (params.hard)
			params.hard_gain =
				(double)(float)((long double)(float)hard *
					(float).7);

		params.scratch = sparks->id[0] != 0;

		if (params.scratch) {
			params.scratch_gain =
				(double)(float)((long double)(float)
							sparks->gain[0] *
					(float)scratch);
			params.pan = (sparks->corner[0] == 0 ||
					sparks->corner[0] == 3)
				? -(double)(float).7
				: (double)(float).7;
		}
	}

	*output = params;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
