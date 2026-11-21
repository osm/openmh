#ifndef MH_SETTINGS_CONFIG_H
#define MH_SETTINGS_CONFIG_H

#include "engine/core/error.h"
#include "engine/network/prediction.h"
#include "engine/settings/av.h"

#define MH_SETTINGS_PLAYER_BYTES 16
#define MH_SETTINGS_TEAM_BYTES 5
#define MH_SETTINGS_CONTROL_BYTES 128
#define MH_SETTINGS_SHORTCUTS 10
#define MH_SETTINGS_SHORTCUT_BYTES 128
#define MH_SETTINGS_CAR_COLORS 3
#define MH_SETTINGS_LEAGUE_BYTES 16
#define MH_SETTINGS_DEMO_BYTES 64
#define MH_SETTINGS_SESSION_BYTES 16
#define MH_SETTINGS_PASSWORD_BYTES 16
#define MH_SETTINGS_HISCORE_BYTES 512
#define MH_SETTINGS_CATALOG_MAX 4096
#define MH_SETTINGS_COURSES 64
#define MH_SETTINGS_CATALOG_NAME_BYTES 128
#define MH_SETTINGS_HORN_NAME_BYTES 16
#define MH_SETTINGS_RATE_MIN 1
#define MH_SETTINGS_RATE_MAX 255
#define MH_SETTINGS_TCP_RATE_DEFAULT 15

/*
 * Catalogs are display names, never file paths. Car/track/horn choose the
 * first match; the original linked renderer walk retains the last match.
 * Each list has 1..4096 entries. Renderer list may be empty, with selection
 * MH_SETTINGS_NO_RENDERER. Names are terminated legacy single-byte text;
 * bytes 32..255 are retained, with ASCII-only case folding.
 * Car/track/horn inputs are joined and truncated to 31 bytes before lookup.
 */
struct mh_settings_names {
	const char	*const *names;
	unsigned int	count;
};

struct mh_settings_catalog {
	struct mh_settings_names	cars, tracks, horns, renderers;
};

#define MH_SETTINGS_NO_RENDERER ((unsigned int)-1)

/*
 * Original CDTrack_<Division> keys; array order follows the installed catalog.
 */
struct mh_settings_course_music {
	unsigned long	division, track;
};

struct mh_settings {
	struct mh_av_settings		av;
	char player_name[MH_SETTINGS_PLAYER_BYTES],
		team_name[MH_SETTINGS_TEAM_BYTES],
		control_name[MH_SETTINGS_CONTROL_BYTES],
		short_keys[MH_SETTINGS_SHORTCUTS][MH_SETTINGS_SHORTCUT_BYTES];

	/* RGB 0xRRGGBB. */
	unsigned long	player_color, car_colors[MH_SETTINGS_CAR_COLORS];
	unsigned int			custom_car_colors, car, automatic, horn;

	/* Single=0/League=1/TimeAttack=2/GhostMode=3. */
	unsigned int			game_type;
	unsigned int	difficulty, track, laps, catch_up, record_race;
	char league_name[MH_SETTINGS_LEAGUE_BYTES],
		demo_filename[MH_SETTINGS_DEMO_BYTES];
	unsigned int			renderer;
	char session_name[MH_SETTINGS_SESSION_BYTES],
		session_password[MH_SETTINGS_PASSWORD_BYTES],
		hiscore_path[MH_SETTINGS_HISCORE_BYTES];
	struct mh_settings_course_music	course_music[MH_SETTINGS_COURSES];
	unsigned int			course_music_count;

	/* RateTCP: requested snapshots per second for native IP sessions. */
	unsigned int			tcp_rate;
	struct mh_net_prediction	prediction;

	/*
	 * baud_rate is an index: 38400,56000,57600,115200,128000,256000. Legacy
	 * transport settings are retained; they do not open network devices.
	 */
	unsigned int protocol, serial_port, baud_rate, parity, stop_bits,
		flow_control;
	unsigned long			menu_cd_track;

	/* Off=0/On=1/NoExit=2. */
	unsigned int			menu_confirmation;
};

/*
 * Platform supplies the username, device volumes and current renderer. Catalog
 * selection starts at index zero.
 */
enum mh_error_code	mh_settings_init(struct mh_settings *settings,
	const char *player_name, unsigned int effects_volume,
	unsigned int music_volume, unsigned int renderer,
	struct mh_error *error_out);

/*
 * Full original CFG reader, including catalog lookup and horn fallback.
 * On success unknown horn names reseed the supplied 32-bit random stream.
 * Unknown horn names shorter than four bytes are rejected, as their original
 * seed depends on stale scratch bytes. All errors preserve settings/random.
 * Input/output settings must be initialized and have valid catalog indices.
 * Legacy paths/names are only stored, never opened or executed.
 */
enum mh_error_code	mh_settings_parse(const unsigned char *text,
	size_t size, const struct mh_settings_catalog *catalog,
	unsigned long *random, struct mh_settings *settings,
	struct mh_error *error_out);

/*
 * Canonical CFG text, with the original field order, enum spellings and
 * numeric conversions. Comments/spacing differ from the original writer.
 * Capacity includes a terminating NUL; size excludes it. NULL text with
 * zero capacity queries the required size. Errors preserve text and size.
 * Saving and reloading can round values through the text representation.
 */
#define MH_SETTINGS_TEXT_CAPACITY 8192
enum mh_error_code	mh_settings_encode(const struct mh_settings *settings,
	const struct mh_settings_catalog *catalog, char *text, size_t capacity,
	size_t *size, struct mh_error *error_out);

#endif
