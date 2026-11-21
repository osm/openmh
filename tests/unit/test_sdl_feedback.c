#include <stdio.h>
#include <string.h>

#include "../support/sdl_feedback.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "SDL feedback line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

/*
 * Exercise the real adapter without making a physical wheel a test dependency.
 */
static struct {
	SDL_HapticEffect	created[MH_FORCE_EFFECTS], updated[MH_FORCE_EFFECTS];
	unsigned int	creates, updates, runs, stops, destroys, closes, quits;
	unsigned int		queries, inits, supported;
	Uint32			features;
	Uint64			now;
	int	fail_init, fail_open, fail_create, fail_update, fail_run;
	int	playing, gain, autocenter, last_update, last_run, bad_cleanup;
} device;

static int joystick_token, haptic_token;

int SDLCALL
SDL_InitSubSystem(SDL_InitFlags flags)
{
	(void)flags;
	device.inits++;

	return !device.fail_init;
}

void SDLCALL
SDL_QuitSubSystem(SDL_InitFlags flags)
{
	(void)flags;
	device.quits++;
}

int SDLCALL
SDL_IsJoystickHaptic(SDL_Joystick *joystick)
{
	return joystick == (SDL_Joystick *)&joystick_token;
}

SDL_Haptic *SDLCALL
SDL_OpenHapticFromJoystick(SDL_Joystick *joystick)
{
	(void)joystick;

	return device.fail_open ? NULL : (SDL_Haptic *)&haptic_token;
}

Uint32 SDLCALL
SDL_GetHapticFeatures(SDL_Haptic *haptic)
{
	(void)haptic;

	return device.features;
}

int SDLCALL
SDL_SetHapticGain(SDL_Haptic *haptic, int gain)
{
	(void)haptic;
	device.gain = gain;

	return 1;
}

int SDLCALL
SDL_SetHapticAutocenter(SDL_Haptic *haptic, int autocenter)
{
	(void)haptic;
	device.autocenter = autocenter;

	return 1;
}

int SDLCALL
SDL_HapticEffectSupported(SDL_Haptic *haptic, const SDL_HapticEffect *effect)
{
	(void)haptic;

	return (device.supported & effect->type) != 0;
}

SDL_HapticEffectID SDLCALL
SDL_CreateHapticEffect(SDL_Haptic *haptic, const SDL_HapticEffect *effect)
{
	unsigned int index;

	(void)haptic;
	index = device.creates++;
	device.created[index] = *effect;

	return device.fail_create ? -1 : (SDL_HapticEffectID)index;
}

Uint64 SDLCALL
SDL_GetTicks(void)
{
	return device.now;
}

int SDLCALL
SDL_GetHapticEffectStatus(SDL_Haptic *haptic, SDL_HapticEffectID effect)
{
	(void)haptic;
	(void)effect;
	device.queries++;

	return device.playing != 0;
}

int SDLCALL
SDL_UpdateHapticEffect(SDL_Haptic *haptic, SDL_HapticEffectID id,
	const SDL_HapticEffect *effect)
{
	(void)haptic;

	device.updates++;
	device.last_update = id;
	device.updated[id] = *effect;

	return !device.fail_update;
}

int SDLCALL
SDL_RunHapticEffect(
	SDL_Haptic *haptic, SDL_HapticEffectID id, Uint32 iterations)
{
	(void)haptic;

	device.runs++;
	device.last_run = id;

	return iterations == 1 && !device.fail_run;
}

int SDLCALL
SDL_StopHapticEffects(SDL_Haptic *haptic)
{
	(void)haptic;
	device.stops++;

	return 1;
}

void SDLCALL
SDL_DestroyHapticEffect(SDL_Haptic *haptic, SDL_HapticEffectID id)
{
	(void)haptic;
	(void)id;

	if (!device.stops || device.closes)
		device.bad_cleanup = 1;

	device.destroys++;
}

void SDLCALL
SDL_CloseHaptic(SDL_Haptic *haptic)
{
	(void)haptic;

	if (!device.stops ||
		device.destroys != (device.fail_create ? 0 : device.creates))
		device.bad_cleanup = 1;

	device.closes++;
}

const char *SDLCALL
SDL_GetError(void)
{
	return "fixture device failure";
}

static void
reset_device(void)
{
	memset(&device, 0, sizeof(device));
	device.supported =
		SDL_HAPTIC_CONSTANT | SDL_HAPTIC_TRIANGLE | SDL_HAPTIC_SPRING;
	device.features = SDL_HAPTIC_GAIN | SDL_HAPTIC_AUTOCENTER;
}

static int
check_translation(void)
{
	struct mh_sdl_feedback feedback;
	struct mh_force_effect effects[MH_FORCE_EFFECTS];
	struct mh_force_status status;
	SDL_Joystick *joystick;
	unsigned int i;

	reset_device();
	memset(&feedback, 0, sizeof(feedback));
	memset(effects, 0, sizeof(effects));
	joystick = (SDL_Joystick *)&joystick_token;
	effects[0].duration_ms = 150;
	effects[0].magnitude = 10000;
	effects[0].direction[0] = -37;
	effects[0].direction[1] = 100;
	effects[1].duration_ms = MH_FORCE_INFINITE;
	effects[1].magnitude = 2500;
	effects[1].period_us = 50400;
	effects[1].direction[0] = 100;
	effects[2].duration_ms = MH_FORCE_INFINITE;
	effects[2].coefficient[0] = 4000;
	effects[2].coefficient[1] = -2500;
	effects[2].saturation[0] = 4000;
	effects[2].saturation[1] = 2500;
	effects[2].deadband[0] = effects[2].deadband[1] = 100;
	CHECK(mh_sdl_feedback_poll(&feedback, joystick, effects, &status,
		NULL) == MH_ERROR_NONE);
	CHECK(device.creates == 3 && device.runs == 0 &&
		status.generation == 1);
	CHECK(device.gain == 100 && device.autocenter == 100);
	CHECK(device.created[0].constant.level == 32767);
	CHECK(device.created[0].constant.length == 150);
	CHECK(device.created[0].constant.direction.type ==
		SDL_HAPTIC_CARTESIAN);
	CHECK(device.created[0].constant.direction.dir[0] == -37);
	CHECK(device.created[0].constant.direction.dir[1] == 100);
	CHECK(device.created[1].periodic.magnitude == 8191);
	CHECK(device.created[1].periodic.period == 50);
	CHECK(device.created[1].periodic.length == SDL_HAPTIC_INFINITY);
	CHECK(device.created[2].condition.right_coeff[0] == 13106);
	CHECK(device.created[2].condition.left_coeff[1] == -8191);
	CHECK(device.created[2].condition.right_sat[0] == 26214);
	CHECK(device.created[2].condition.left_sat[1] == 16383);
	CHECK(device.created[2].condition.deadband[0] == 655);
	CHECK(device.created[2].condition.deadband[1] == 655);

	for (i = 0; i < MH_FORCE_EFFECTS; i++)
		CHECK(status.available[i] && !status.playing[i]);

	device.now = 10;
	CHECK(mh_sdl_feedback_apply(&feedback, effects, 7, 7, NULL) ==
		MH_ERROR_NONE);
	CHECK(device.updates == 3 && device.runs == 3);
	device.now = 159;
	CHECK(mh_sdl_feedback_poll(&feedback, joystick, effects, &status,
		NULL) == MH_ERROR_NONE);
	CHECK(status.playing[0] && status.playing[1] && status.playing[2]);
	device.now = 160;
	CHECK(mh_sdl_feedback_poll(&feedback, joystick, effects, &status,
		NULL) == MH_ERROR_NONE);
	CHECK(!status.playing[0] && status.playing[1] && status.playing[2]);
	CHECK(device.creates == 3 && device.queries == 0);

	CHECK(mh_sdl_feedback_apply(&feedback, effects, 2, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(device.updates == 4 && device.last_update == 1 &&
		device.runs == 3);
	device.features |= SDL_HAPTIC_STATUS;
	feedback.features = device.features;
	device.playing = 1;
	CHECK(mh_sdl_feedback_poll(&feedback, joystick, effects, &status,
		NULL) == MH_ERROR_NONE);
	CHECK(status.playing[0] && device.queries == 3);
	mh_sdl_feedback_close(&feedback);
	CHECK(device.stops == 1 && device.destroys == 3 && device.closes == 1);
	CHECK(device.autocenter == 0 && device.quits == 1 &&
		!device.bad_cleanup);
	mh_sdl_feedback_close(&feedback);
	CHECK(device.stops == 1 && device.quits == 1);
	reset_device();
	CHECK(mh_sdl_feedback_poll(&feedback, joystick, effects, &status,
		NULL) == MH_ERROR_NONE);
	CHECK(status.generation == 2 && !status.playing[1]);
	mh_sdl_feedback_close(&feedback);

	return 0;
}

static int
check_failures(void)
{
	struct mh_sdl_feedback feedback;
	struct mh_force_effect effects[MH_FORCE_EFFECTS];
	struct mh_force_status status;
	struct mh_error error;
	SDL_Joystick *joystick;
	unsigned int failure;

	joystick = (SDL_Joystick *)&joystick_token;
	memset(effects, 0, sizeof(effects));

	for (failure = 0; failure < 4; failure++) {
		reset_device();
		memset(&feedback, 0, sizeof(feedback));
		device.fail_init = failure == 0;
		device.fail_open = failure == 1;
		device.fail_create = failure == 2;

		if (failure == 3)
			device.supported = SDL_HAPTIC_SPRING;

		CHECK(mh_sdl_feedback_poll(&feedback, NULL, effects, &status,
			NULL) == MH_ERROR_NONE);
		CHECK(device.inits == 0 && !status.available[0]);
		CHECK(mh_sdl_feedback_poll(&feedback, joystick, effects,
			&status, NULL) == MH_ERROR_NONE);
		CHECK(mh_sdl_feedback_poll(&feedback, joystick, effects,
			&status, NULL) == MH_ERROR_NONE);
		CHECK(device.inits == 1 && !status.available[0] &&
			!status.available[1]);
		CHECK(status.available[2] == (failure == 3));

		if (failure == 3) {
			device.fail_update = 1;
			CHECK(mh_sdl_feedback_apply(&feedback, effects, 7, 7,
				&error) == MH_ERROR_BACKEND);
			CHECK(device.runs == 0 &&
				strcmp(error.message,
					"fixture device failure") == 0);
			device.fail_update = 0;
			device.fail_run = 1;
			CHECK(mh_sdl_feedback_apply(&feedback, effects, 4, 4,
				&error) == MH_ERROR_BACKEND);
			CHECK(device.runs == 1 && !feedback.playing[2]);
		}

		mh_sdl_feedback_close(&feedback);
		CHECK(device.quits == (failure != 0));
		CHECK(device.destroys == (failure == 3) && !device.bad_cleanup);
	}

	return 0;
}

int
main(void)
{
	if (check_translation() || check_failures())
		return 1;

	puts("SDL force units, capability subsets, playback and cleanup passed");

	return 0;
}
