#ifndef MH_CLIENT_DEBUG_RACE_OPTIONS_H
#define MH_CLIENT_DEBUG_RACE_OPTIONS_H

struct mh_debug_options;

enum {
	MH_DEBUG_RACE_MAX_PERCENT = 100,
	MH_DEBUG_RACE_TEAM_LENGTH = 4
};

enum mh_debug_race_parse_result {
	MH_DEBUG_RACE_INVALID,
	MH_DEBUG_RACE_HELP,
	MH_DEBUG_RACE_READY
};

/* Strings borrow argv or literals; the launcher may resolve the horn name. */
struct mh_debug_race_options {
	const char	*asset_root, *capture_path, *replay_path, *trace_path;
	const char	*player_name, *horn, *team, *records_path, *recording_path;
	const char	*settings_path, *controls_path, *music_root;
	unsigned long	car_number, track_number, grid_slot, ticks, lap_count;
	unsigned long	ai_difficulty, opponent_count, difficulty;
	unsigned long	effects_percent, music_percent;

	/* Catalog count means no command-line override. */
	unsigned int	renderer;
	int	manual, scenery, headless, check, countdown, results, mute;
	int		spatial_audio;
	int	list_horns, horn_selected, no_records, time_attack, catch_up;
	int		no_settings, effects_override, music_override;
};

void	mh_debug_race_usage(void);

/* Syntax only: horn listing/lookup precedes semantic validation. */
enum mh_debug_race_parse_result	mh_debug_race_parse(int argc, char *argv[],
	struct mh_debug_race_options *options, struct mh_debug_options *debug);

/* Print the first diagnostic; publish derived cheat flags only on success. */
int	mh_debug_race_validate(const struct mh_debug_race_options *options,
	unsigned int *cheats);

#endif
