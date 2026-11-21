#ifndef MH_MENU_PREVIEW_H
#define MH_MENU_PREVIEW_H
/*
 * Finite fade in [0,1], elapsed in milliseconds. A new selection becomes
 * visible only when the previous fade is zero at the start of the step.
 * Return the clamped fade, retaining extended arithmetic before float storage.
 */
float	mh_menu_preview_advance(unsigned int selected, unsigned long elapsed_ms,
	unsigned int *shown, float fade);
#endif
