#ifndef MH_TEST_SDL_FEEDBACK_H
#define MH_TEST_SDL_FEEDBACK_H

#include "backends/input/sdl_feedback.h"

/*
 * Keep SDK structures and non-boolean signatures real. Redirect boolean calls
 * to C89 int stubs so the fixture need not define SDL's C99 bool functions.
 */
#define SDL_InitSubSystem mh_test_InitSubSystem
int SDLCALL mh_test_InitSubSystem(SDL_InitFlags flags);
#define SDL_IsJoystickHaptic mh_test_IsJoystickHaptic
int SDLCALL mh_test_IsJoystickHaptic(SDL_Joystick *joystick);
#define SDL_SetHapticGain mh_test_SetHapticGain
int SDLCALL mh_test_SetHapticGain(SDL_Haptic *haptic, int gain);
#define SDL_SetHapticAutocenter mh_test_SetHapticAutocenter
int SDLCALL mh_test_SetHapticAutocenter(SDL_Haptic *haptic, int autocenter);
#define SDL_HapticEffectSupported mh_test_HapticEffectSupported
int SDLCALL mh_test_HapticEffectSupported(
	SDL_Haptic *haptic, const SDL_HapticEffect *effect);
#define SDL_GetHapticEffectStatus mh_test_GetHapticEffectStatus
int SDLCALL mh_test_GetHapticEffectStatus(
	SDL_Haptic *haptic, SDL_HapticEffectID effect);
#define SDL_UpdateHapticEffect mh_test_UpdateHapticEffect
int SDLCALL mh_test_UpdateHapticEffect(SDL_Haptic *haptic,
	SDL_HapticEffectID id, const SDL_HapticEffect *effect);
#define SDL_RunHapticEffect mh_test_RunHapticEffect
int SDLCALL mh_test_RunHapticEffect(
	SDL_Haptic *haptic, SDL_HapticEffectID id, Uint32 iterations);
#define SDL_StopHapticEffects mh_test_StopHapticEffects
int SDLCALL mh_test_StopHapticEffects(SDL_Haptic *haptic);

#endif
