#include <string.h>

#include "backends/input/sdl_feedback.h"
#include "engine/core/limits.h"

#define SIGNED_FORCE_MAX 32767L
#define UNSIGNED_FORCE_MAX 65535UL

static long
signed_force(long value)
{
	if (value < -MH_FORCE_SCALE)
		value = -MH_FORCE_SCALE;

	if (value > MH_FORCE_SCALE)
		value = MH_FORCE_SCALE;

	return value * SIGNED_FORCE_MAX / MH_FORCE_SCALE;
}

static unsigned long
unsigned_force(unsigned long value)
{
	if (value > MH_FORCE_SCALE)
		value = MH_FORCE_SCALE;

	return value * UNSIGNED_FORCE_MAX / MH_FORCE_SCALE;
}

static void
translate_effect(enum mh_force_kind kind, const struct mh_force_effect *source,
	SDL_HapticEffect *effect)
{
	SDL_HapticDirection direction;
	unsigned int axis;
	unsigned long period_ms;
	long value;

	memset(effect, 0, sizeof(*effect));
	memset(&direction, 0, sizeof(direction));
	direction.type = SDL_HAPTIC_CARTESIAN;

	for (axis = 0; axis < 2; axis++) {
		value = source->direction[axis];

		if (value > MH_I32_MAX)
			value = MH_I32_MAX;

		if (value < MH_I32_MIN)
			value = MH_I32_MIN;

		direction.dir[axis] = (Sint32)value;
	}

	switch (kind) {
	case MH_FORCE_CONSTANT:
		effect->constant.type = SDL_HAPTIC_CONSTANT;
		effect->constant.direction = direction;
		effect->constant.length = (Uint32)source->duration_ms;
		effect->constant.level =
			(Sint16)signed_force(source->magnitude);
		break;

	case MH_FORCE_TRIANGLE:
		period_ms = source->period_us / 1000;
		if (period_ms == 0)
			period_ms = 1;

		if (period_ms > MH_U16_MAX)
			period_ms = MH_U16_MAX;

		effect->periodic.type = SDL_HAPTIC_TRIANGLE;
		effect->periodic.direction = direction;
		effect->periodic.length = (Uint32)source->duration_ms;
		effect->periodic.period = (Uint16)period_ms;
		effect->periodic.magnitude =
			(Sint16)signed_force(source->magnitude);
		break;

	case MH_FORCE_SPRING:
		effect->condition.type = SDL_HAPTIC_SPRING;
		effect->condition.length = (Uint32)source->duration_ms;

		for (axis = 0; axis < 2; axis++) {
			effect->condition.right_coeff[axis] =
				(Sint16)signed_force(source->coefficient[axis]);
			effect->condition.left_coeff[axis] =
				effect->condition.right_coeff[axis];
			effect->condition.right_sat[axis] =
				(Uint16)unsigned_force(
					source->saturation[axis]);
			effect->condition.left_sat[axis] =
				effect->condition.right_sat[axis];
			effect->condition.deadband[axis] =
				(Uint16)unsigned_force(source->deadband[axis]);
		}

		break;

	default:
		break;
	}
}

void
mh_sdl_feedback_close(struct mh_sdl_feedback *feedback)
{
	unsigned int slot;
	unsigned long generation;

	/* Keep the generation so callers detect recreated effects. */
	generation = feedback->generation;

	if (feedback->device != NULL) {
		SDL_StopHapticEffects(feedback->device);

		if (feedback->features & SDL_HAPTIC_AUTOCENTER)
			SDL_SetHapticAutocenter(feedback->device, 0);

		for (slot = 0; slot < MH_FORCE_EFFECTS; slot++)
			if (feedback->effects[slot] >= 0)
				SDL_DestroyHapticEffect(feedback->device,
					feedback->effects[slot]);

		SDL_CloseHaptic(feedback->device);
	}

	if (feedback->initialized)
		SDL_QuitSubSystem(SDL_INIT_HAPTIC);

	memset(feedback, 0, sizeof(*feedback));
	feedback->generation = generation;
}

static void
open_feedback(struct mh_sdl_feedback *feedback, SDL_Joystick *joystick,
	const struct mh_force_effect initial[MH_FORCE_EFFECTS])
{
	SDL_HapticEffect effect;
	unsigned int slot;

	if (feedback->attempted || joystick == NULL)
		return;

	feedback->attempted = 1;

	if (!SDL_InitSubSystem(SDL_INIT_HAPTIC))
		return;

	feedback->initialized = 1;

	if (!SDL_IsJoystickHaptic(joystick))
		return;

	feedback->device = SDL_OpenHapticFromJoystick(joystick);

	if (feedback->device == NULL)
		return;

	feedback->generation++;
	feedback->features = SDL_GetHapticFeatures(feedback->device);

	if (feedback->features & SDL_HAPTIC_GAIN)
		SDL_SetHapticGain(feedback->device, 100);

	if (feedback->features & SDL_HAPTIC_AUTOCENTER)
		SDL_SetHapticAutocenter(feedback->device, 0);

	for (slot = 0; slot < MH_FORCE_EFFECTS; slot++) {
		feedback->effects[slot] = -1;
		translate_effect(
			(enum mh_force_kind)slot, &initial[slot], &effect);

		if (SDL_HapticEffectSupported(feedback->device, &effect))
			feedback->effects[slot] = SDL_CreateHapticEffect(
				feedback->device, &effect);

		feedback->duration[slot] = initial[slot].duration_ms;
	}
}

enum mh_error_code
mh_sdl_feedback_poll(struct mh_sdl_feedback *feedback, SDL_Joystick *joystick,
	const struct mh_force_effect initial[MH_FORCE_EFFECTS],
	struct mh_force_status *output, struct mh_error *error_out)
{
	unsigned int slot, available;
	Uint64 now;

	memset(output, 0, sizeof(*output));
	open_feedback(feedback, joystick, initial);
	output->generation = feedback->generation;

	if (feedback->device == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	now = SDL_GetTicks();
	available = 0;

	for (slot = 0; slot < MH_FORCE_EFFECTS; slot++) {
		if (feedback->effects[slot] < 0)
			continue;

		available++;
		output->available[slot] = 1;

		if (feedback->features & SDL_HAPTIC_STATUS)
			output->playing[slot] = SDL_GetHapticEffectStatus(
				feedback->device, feedback->effects[slot]);
		else
			output->playing[slot] = feedback->playing[slot] &&
				(feedback->duration[slot] ==
						MH_FORCE_INFINITE ||
					now - feedback->started[slot] <
						feedback->duration[slot]);
	}

	/*
	 * Original frame enables automatic centering after setup disabled it.
	 */
	if (available && (feedback->features & SDL_HAPTIC_AUTOCENTER))
		SDL_SetHapticAutocenter(feedback->device, 100);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sdl_feedback_apply(struct mh_sdl_feedback *feedback,
	const struct mh_force_effect effects[MH_FORCE_EFFECTS],
	unsigned int changed, unsigned int start, struct mh_error *error_out)
{
	SDL_HapticEffect effect;
	unsigned int slot;

	if (feedback->device == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (slot = 0; slot < MH_FORCE_EFFECTS; slot++) {
		if (!(changed & (1U << slot)) || feedback->effects[slot] < 0)
			continue;

		translate_effect(
			(enum mh_force_kind)slot, &effects[slot], &effect);

		if (!SDL_UpdateHapticEffect(
			feedback->device, feedback->effects[slot], &effect))
			return mh_error_set(
				error_out, MH_ERROR_BACKEND, SDL_GetError());

		feedback->duration[slot] = effects[slot].duration_ms;

		if (start & (1U << slot)) {
			if (!SDL_RunHapticEffect(feedback->device,
				feedback->effects[slot], 1))
				return mh_error_set(error_out, MH_ERROR_BACKEND,
					SDL_GetError());

			feedback->playing[slot] = 1;
			feedback->started[slot] = SDL_GetTicks();
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
