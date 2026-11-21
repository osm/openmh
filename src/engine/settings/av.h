#ifndef MH_SETTINGS_AV_H
#define MH_SETTINGS_AV_H

#include <stddef.h>

#include "engine/audio/race_profile.h"
#include "engine/core/error.h"

enum mh_unit_system {
	MH_UNITS_METRIC,
	MH_UNITS_IMPERIAL
};

enum mh_car_view {
	MH_CAR_VIEW_INSIDE,
	MH_CAR_VIEW_OUTSIDE
};

enum mh_detail_mode {
	MH_DETAIL_LOW,
	MH_DETAIL_MEDIUM,
	MH_DETAIL_HIGH,
	MH_DETAIL_MAX,
	MH_DETAIL_CUSTOM
};

#define MH_AV_COLOR_CHANNELS 3
#define MH_AV_VOLUME_MAX 255
#define MH_AV_MAX_FPS 10000UL
#define MH_AV_CHECKPOINT_DELAY_MAX 10000UL
#define MH_AV_VIEW_DISTANCE_MIN 30
#define MH_AV_VIEW_DISTANCE_MAX 150
#define MH_AV_BRIGHTNESS_MAX 2

enum mh_av_info {
	MH_AV_INFO_NONE,
	MH_AV_INFO_SELECTIVE,
	MH_AV_INFO_ALL
};

enum mh_av_name_plates {
	MH_AV_NAMES_NONE,
	MH_AV_NAMES_FLAT,
	MH_AV_NAMES_TRANSPARENT
};

enum mh_av_shading {
	MH_AV_SHADING_FLAT,
	MH_AV_SHADING_GOURAUD,
	MH_AV_SHADING_REFLECTION,
	MH_AV_SHADING_GLENZ
};

enum mh_av_texture_precision {
	MH_AV_TEXTURE_16BIT,
	MH_AV_TEXTURE_32BIT
};

enum mh_av_texture_filter {
	MH_AV_FILTER_ORIGINAL,
	MH_AV_FILTER_NEAREST,
	MH_AV_FILTER_BILINEAR,
	MH_AV_FILTER_TRILINEAR
};

enum mh_av_frame_queue {
	MH_AV_FRAME_QUEUE_ONE,
	MH_AV_FRAME_QUEUE_TWO,
	MH_AV_FRAME_QUEUE_THREE
};

enum mh_screenshot_format {
	MH_SCREENSHOT_PNG,
	MH_SCREENSHOT_TGA,
	MH_SCREENSHOT_FORMAT_COUNT
};

enum mh_av_perspective {
	MH_AV_PERSPECTIVE_GOOD = 16,
	MH_AV_PERSPECTIVE_NORMAL = 32
};

/*
 * Portable graphics/audio settings. Other menu settings are in
 * engine/settings/config.h. Device enumeration remains a platform concern.
 */
struct mh_av_settings {
	/* Display units only; physics remains metric. */
	unsigned int	system;

	/* Saved Inside/Outside preference. */
	unsigned int	car_view;

	/* Native borderless fullscreen preference. */
	unsigned int	fullscreen;

	/* Confine/hide the mouse in windowed menus and races. */
	unsigned int	grab_mouse;

	/* Race presentation only; VSync defaults on, max_fps=0 means no cap. */
	unsigned int	vsync;
	unsigned long	max_fps;

	/* PNG by default; independent of detail presets. */
	unsigned int	screenshot_format;
	unsigned int	show_fps;	     /* Render-rate overlay. */
	unsigned long	width, height; /* Original unsigned 32-bit values. */

	/* Ratios, upper clamp 2; original has no lower clamp. */
	float		brightness[MH_AV_COLOR_CHANNELS];

	/* None/Selective/All. */
	unsigned int	info_detail, checkpoint_info, road_map;
	unsigned long	checkpoint_delay; /* Milliseconds, 0..10000. */
	unsigned int	detail_mode;
	unsigned int	lens_flares, sparks, smoke, halos, skid_marks, shadow;
	unsigned int	name_plates; /* None/Flat/Transparent. */
	unsigned int	sky_image;	  /* Off/On. */

	/* Medium/High; Low/Medium/High. */
	unsigned int	track_detail, car_detail;
	unsigned int	car_shading; /* Flat/Gouraud/Reflection/Glenz. */

	/* Native opaque texture precision; fixed when scene assets load. */
	unsigned int	texture_precision;

	/* Saved GPU race queue choice (one/two/three). */
	unsigned int	frame_queue;
	unsigned int	texture_filter; /* Native GPU scene filtering. */

	/* Selective/All; Normal=32/Good=16. */
	unsigned int	perspective_on, perspective;
	unsigned int	motion_blur;

	/*
	 * Original TrippleBuffer preference; Direct3D retained color history.
	 */
	unsigned int	triple_buffer;
	float		view_distance; /* Percent, 30..150. */

	/* Native race effects profile; Software by default. */
	unsigned int	race_audio;

	/* 0..255, 0..255, Off/On. */
	unsigned int	effects_volume, music_volume, cd_loop;
};

/* Defaults. Device volume queries are explicit inputs. */
enum mh_error_code	mh_av_settings_init(struct mh_av_settings *settings,
	unsigned int effects_volume, unsigned int music_volume,
	struct mh_error *error_out);

/* Custom retains all individual values. Load does not call this. */
enum mh_error_code	mh_av_settings_preset(struct mh_av_settings *settings,
	struct mh_error *error_out);

/*
 * Patch the supplied settings from bounded legacy byte text. Unknown keys/enums
 * and key-only lines are ignored; later recognized values win. Accepts LF/CRLF,
 * space/tab separators and ; or // comments. No quoted strings. Errors preserve
 * settings. No file or rendering side effects. Numeric input is bounded.
 */
enum mh_error_code	mh_av_settings_parse(const unsigned char *text,
	size_t size, struct mh_av_settings *settings,
	struct mh_error *error_out);

int		mh_screenshot_format_available(unsigned int format);
unsigned int	mh_screenshot_format_default(void);

#endif
