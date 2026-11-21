#ifndef MH_SETTINGS_CONTROL_LAYOUT_H
#define MH_SETTINGS_CONTROL_LAYOUT_H

#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"

#define MH_CONTROL_BINDINGS 28
#define MH_CONTROL_NAME_BYTES 32
#define MH_CONTROL_KEY_NAME_BYTES 32
#define MH_CONTROL_MOUSE_AXES 3
#define MH_CONTROL_UNBOUND MH_U32_MAX
#define MH_CONTROL_TEXT_CAPACITY 16384
#define MH_CONTROL_PROFILES 4096

/* Action order; numeric keys are fixed defaults, not CLO keys. */
enum mh_control_action {
	MH_CONTROL_LEFT,
	MH_CONTROL_RIGHT,
	MH_CONTROL_ACCELERATE,
	MH_CONTROL_BRAKE,
	MH_CONTROL_SHIFT_UP,
	MH_CONTROL_SHIFT_DOWN,
	MH_CONTROL_HORN,
	MH_CONTROL_HANDBRAKE,
	MH_CONTROL_REAR_VIEW,
	MH_CONTROL_GAME_MENU,
	MH_CONTROL_INFO,
	MH_CONTROL_CHAT,
	MH_CONTROL_IN_CAR,
	MH_CONTROL_OUT_CAR,
	MH_CONTROL_CAMERA,
	MH_CONTROL_MIRROR,
	MH_CONTROL_CYCLE,
	MH_CONTROL_SCREENSHOT,
	MH_CONTROL_DIGIT_0,
	MH_CONTROL_DIGIT_1,
	MH_CONTROL_DIGIT_2,
	MH_CONTROL_DIGIT_3,
	MH_CONTROL_DIGIT_4,
	MH_CONTROL_DIGIT_5,
	MH_CONTROL_DIGIT_6,
	MH_CONTROL_DIGIT_7,
	MH_CONTROL_DIGIT_8,
	MH_CONTROL_DIGIT_9
};

/* Only the actions before numeric shortcuts are read/written as CLO keys. */
#define MH_CONTROL_FILE_BINDINGS MH_CONTROL_DIGIT_0

struct mh_control_layout {
	char		name[MH_CONTROL_NAME_BYTES];
	float		mouse_speed[MH_CONTROL_MOUSE_AXES];
	unsigned int	force_feedback;
	unsigned long	bindings[MH_CONTROL_BINDINGS];
};

struct mh_control_profile {
	struct mh_control_layout	layout;

	/* Dirty flag; the writer tests exactly 1. */
	unsigned long			dirty;
};

/*
 * Apply bindings: the first 18 codes become device/direction/button words;
 * digit bindings 18..27 remain raw scan codes. MouseIn/Out and JoyIn/Out select
 * Z; values above 255 (including unbound) yield zero.
 */
struct mh_control_binding_set {
	float		mouse_speed[MH_CONTROL_MOUSE_AXES];
	unsigned int	force_feedback;
	unsigned long	codes[MH_CONTROL_BINDINGS];
};

/*
 * Stable native names: patched fallback table with Esc/Ctrl/Shift/Alt as
 * used by the shipped layouts. No OS key-name lookup in portable logic.
 */
const char	*mh_control_key_name(unsigned int code);

/*
 * Optional 256-entry catalog contains already space-stripped labels. NULL
 * selects the stable built-in catalog. The built-in catalog also reads older
 * UndefinedE9/EA spellings for MouseIn/Out. Unknown names retain defaults.
 * Failure leaves output unchanged.
 */
enum mh_error_code	mh_control_layout_read(const unsigned char *text,
	size_t size, const char *const *key_names,
	struct mh_control_layout *output, struct mh_error *error_out);

/*
 * Catalog reader uses original Layout%02u defaults, with one-based ordinal.
 * The single-layout interface above is equivalent to ordinal 1.
 */
enum mh_error_code	mh_control_layout_read_at(const unsigned char *text,
	size_t size, const char *const *key_names, unsigned int ordinal,
	struct mh_control_layout *output, struct mh_error *error_out);

enum mh_error_code	mh_control_layout_apply(
	const struct mh_control_layout *layout,
	struct mh_control_binding_set *output, struct mh_error *error_out);

/*
 * Patched writer's X/Y/Z speeds, ForceFeedback and %f formatting; the reader
 * also accepts legacy MouseSpeedH/V aliases in file order. ForceFeedback is
 * a saved preference, not a claim of device support. Uses a native header
 * and LF newlines. Writes low-byte names for actions 0..17, then a comment
 * catalog of valid names; fixed digit bindings are not saved. Optional
 * 256-entry key_names must contain printable labels of at most 31 bytes.
 * Layout names may contain printable comments/spaces; the original reader
 * has lossy reload rules for these. Validate and encode before publishing.
 * NULL text/capacity 0 queries size; capacity includes a terminating NUL.
 */
enum mh_error_code	mh_control_layout_encode(
	const struct mh_control_layout *layout, const char *const *key_names,
	char *text, size_t capacity, size_t *size, struct mh_error *error_out);

#endif
