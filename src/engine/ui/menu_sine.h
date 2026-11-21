#ifndef MH_MENU_SINE_H
#define MH_MENU_SINE_H

#define MH_MENU_SINE_QUARTER_TURN 1024
#define MH_MENU_SINE_FULL_TURN (4 * MH_MENU_SINE_QUARTER_TURN)
#define MH_MENU_SINE_PHASE_MASK (MH_MENU_SINE_FULL_TURN - 1U)

/*
 * Phase wraps every full turn. Menu sine values are rounded to seven
 * decimal places before conversion to float.
 */
float	mh_menu_sine(unsigned int phase);

#endif
