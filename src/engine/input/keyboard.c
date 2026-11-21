#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/devices.h"
#include "engine/input/keyboard.h"

#define REPEAT_DELAY_MS 400UL
#define REPEAT_INTERVAL_MS 100UL
#define STARTUP_POLLS 4

void
mh_keyboard_reset(struct mh_keyboard *keyboard)
{
	if (keyboard == NULL)
		return;

	keyboard->clock = 0;
	keyboard->startup = 0;
	keyboard->held_count = keyboard->event_count = 0;
	memset(keyboard->pending, 0, sizeof(keyboard->pending));
	memset(keyboard->held, 0, sizeof(keyboard->held));
	memset(keyboard->events, 0, sizeof(keyboard->events));
	memset(keyboard->held_keys, 0, sizeof(keyboard->held_keys));
	memset(keyboard->event_keys, 0, sizeof(keyboard->event_keys));
}

enum mh_error_code
mh_keyboard_init(struct mh_keyboard *keyboard, struct mh_error *error_out)
{
	if (keyboard == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing keyboard state");

	memset(keyboard, 0, sizeof(*keyboard));
	keyboard->delay = REPEAT_DELAY_MS;
	keyboard->interval = REPEAT_INTERVAL_MS;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_keyboard_configure(struct mh_keyboard *keyboard, unsigned long delay,
	unsigned long interval, struct mh_error *error_out)
{
	if (keyboard == NULL || delay > MH_U32_MAX || interval > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid keyboard repeat configuration");

	keyboard->delay = delay;
	keyboard->interval = interval == 0 ? REPEAT_INTERVAL_MS : interval;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
update_events(
	struct mh_keyboard *keyboard, const unsigned char down[MH_KEY_COUNT])
{
	unsigned long overdue;
	unsigned int i;
	int suppress, pressed, was_down;

	keyboard->held_count = keyboard->event_count = 0;
	suppress = keyboard->startup < STARTUP_POLLS;

	if (suppress)
		keyboard->startup++;

	for (i = 0; i < MH_KEY_COUNT; i++) {
		pressed = (down[i] & MH_KEY_DOWN_BIT) != 0;
		was_down = keyboard->raw_down[i];
		keyboard->held[i] = !suppress && pressed;
		keyboard->events[i] = 0;

		if (!pressed || suppress) {
			if (!pressed)
				keyboard->raw_down[i] = 0;

			keyboard->pending[i] = keyboard->delay;
			continue;
		}

		keyboard->raw_down[i] = 1;
		keyboard->held_keys[keyboard->held_count++] = (unsigned char)i;

		/* Confirmation and Caps Lock only act on a fresh press. */
		if (was_down &&
			(i == MH_KEY_ENTER || i == MH_KEY_ESCAPE ||
				i == MH_KEY_CAPS))
			continue;

		if (!was_down) {
			keyboard->events[i] = 1;
			keyboard->pending[i] = keyboard->delay;
		} else if (keyboard->elapsed >= keyboard->pending[i]) {
			/*
			 * Keep the cadence across polls, without replaying a
			 * stall.
			 */
			overdue = keyboard->elapsed - keyboard->pending[i];
			keyboard->pending[i] = keyboard->interval -
				overdue % keyboard->interval;
			keyboard->events[i] = 1;
		} else
			keyboard->pending[i] -= keyboard->elapsed;

		if (keyboard->events[i])
			keyboard->event_keys[keyboard->event_count++] =
				(unsigned char)i;
	}

	if (keyboard->events[MH_KEY_CAPS])
		keyboard->caps = 1 - keyboard->caps;
}

static enum mh_error_code
poll_input(struct mh_keyboard *keyboard, const unsigned char down[MH_KEY_COUNT],
	unsigned long now_ms, struct mh_device_keys *devices,
	const struct mh_device_input *input, unsigned long mode,
	struct mh_error *error_out)
{
	struct mh_keyboard next;
	struct mh_device_keys next_devices;
	unsigned char combined[MH_KEY_COUNT];
	enum mh_error_code status;
	unsigned int i;

	if (keyboard == NULL || down == NULL || now_ms > MH_U32_MAX ||
		keyboard->delay > MH_U32_MAX || keyboard->interval == 0 ||
		keyboard->interval > MH_U32_MAX ||
		keyboard->clock > MH_U32_MAX ||
		keyboard->previous > MH_U32_MAX ||
		keyboard->startup > STARTUP_POLLS ||
		(keyboard->caps != 0 && keyboard->caps != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid keyboard poll");

	for (i = 0; i < MH_KEY_COUNT; i++)
		if (keyboard->pending[i] > MH_U32_MAX ||
			keyboard->raw_down[i] > 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid keyboard timer");

	next = *keyboard;

	if (next.clock == 0) {
		next.clock = 1;
		next.previous = now_ms;
	}

	next.elapsed = (now_ms - next.previous) & MH_U32_MAX;
	next.previous = now_ms;
	next.clock = (next.clock + next.elapsed) & MH_U32_MAX;

	if (devices != NULL) {
		next_devices = *devices;
		status = mh_device_keys_poll(&next_devices, input, down,
			next.clock, keyboard->clock == 0, mode, combined,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		down = combined;
	}

	update_events(&next, down);

	*keyboard = next;

	if (devices != NULL)
		*devices = next_devices;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_keyboard_poll(struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now_ms,
	struct mh_error *error_out)
{
	return poll_input(keyboard, down, now_ms, NULL, NULL, 0, error_out);
}

enum mh_error_code
mh_keyboard_poll_devices(struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now_ms,
	struct mh_device_keys *devices, const struct mh_device_input *input,
	unsigned long mode, struct mh_error *error_out)
{
	if (devices == NULL || input == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing device input");

	return poll_input(
		keyboard, down, now_ms, devices, input, mode, error_out);
}
