#ifndef MH_CATALOG_TRACK_H
#define MH_CATALOG_TRACK_H

#include <stddef.h>

#include "interfaces/catalog/halo_paths.h"

/* Add-on identities are outside the original league division namespace. */
#define MH_TRACK_CUSTOM_DIVISION 100000UL

#define MH_TRACK_PATH_SIZE 256
#define MH_TRACK_TEXT_SIZE 256
#define MH_TRACK_ENVIRONMENTS 3
#define MH_TRACK_DIFFICULTIES 3
#define MH_TRACK_AI_VALUES 11

/* Positions within each AI1/AI2/AI3 record. */
enum mh_track_ai_parameter {
	MH_TRACK_AI_SPEED_LEVEL = 0,
	MH_TRACK_AI_SPEED_WEIGHT = 1,
	MH_TRACK_AI_ACCEL_LEVEL = 2,
	MH_TRACK_AI_ACCEL_WEIGHT = 3,
	MH_TRACK_AI_GRIP_LEVEL = 4,
	MH_TRACK_AI_GRIP_WEIGHT = 5,
	MH_TRACK_AI_BRAKE_FORCE = 6,
	MH_TRACK_AI_BRAKE_WEIGHT = 7,
	MH_TRACK_AI_CORNER_GRIP = 8,
	MH_TRACK_AI_CORNER_STEERING = 9,
	MH_TRACK_AI_CATCHUP = 10
};

/*
 * Decoded track definition: resources, visuals, grid and race tuning.
 * The value owns its inline names and arrays; paths are logical resources.
 * Shipped tracks have three EnvMapName entries; rendering selects the first.
 */
struct mh_track {
	int			has_division;

	/* TRK Division. */
	unsigned long		division;

	/* TrackName, used by the original network roster lookup. */
	char			name[128];

	/* Authored loading-screen text; lengths are not physics distances. */
	char			location[MH_TRACK_TEXT_SIZE];
	char			length[MH_TRACK_TEXT_SIZE];
	char			length_us[MH_TRACK_TEXT_SIZE];
	char			environment[MH_TRACK_ENVIRONMENTS][MH_TRACK_PATH_SIZE];

	/* Empty if absent in the decoded subset. */
	char			base_path[MH_TRACK_PATH_SIZE];
	char			world_file[MH_TRACK_PATH_SIZE];
	char			scene_file[MH_TRACK_PATH_SIZE];
	char			collision_file[MH_TRACK_PATH_SIZE];
	char			materials_file[MH_TRACK_PATH_SIZE];
	char			spline_file[MH_TRACK_PATH_SIZE];

	/* CameraSplineName; root-relative camera LWMO. */
	char			camera_file[MH_TRACK_PATH_SIZE];

	/* StartSpline1/2, relative to BasePath. */
	char			start_camera[2][MH_TRACK_PATH_SIZE];

	/* AITrackName; append ai/ai.dat. */
	char			ai_path[MH_TRACK_PATH_SIZE];

	/* EnvSoundName; root-relative ESD. */
	char			env_sound_file[MH_TRACK_PATH_SIZE];

	/* BackgroundName, original panorama. */
	char			background[MH_TRACK_PATH_SIZE];

	/* TrackMapName, root-relative basename; append .iff. */
	char			map_name[MH_TRACK_PATH_SIZE];

	/* LineObjectName, menu LOB. */
	char			line_object[MH_TRACK_PATH_SIZE];
	int			has_map_info;

	/* TrackMapInfo: camera XYZ and angle in degrees. */
	float			map_info[4];

	/* Packed NUL-terminated ZeroTransparent names. */
	char			zero_transparent[1024];

	/* Includes each name's NUL, excludes the final sentinel. */
	size_t			zero_transparent_size;
	unsigned char		sky_top[3], sky_bottom[3];

	/* Binary32 Horizon, fraction of viewport height. */
	double			horizon;

	/* Bits 1 top, 2 bottom, 4 horizon. */
	unsigned int		sky_settings;
	int			has_ai[MH_TRACK_DIFFICULTIES];

	/* AI1/AI2/AI3, original binary32 values. */
	double			ai[MH_TRACK_DIFFICULTIES][MH_TRACK_AI_VALUES];

	/* Native defaults: brightness 1,1,1; ambient 0,0,0; intensity 1. */
	double			world_brightness[3];
	double			object_brightness[3];

	/* 3DAccBrightness, binary32, defaults to 1,1,1. */
	double			texture_brightness[3];
	double			object_ambient[3];
	double			light_intensity;

	/* SpecularFactor, binary32; native default 1. */
	double			specular_factor;

	/* ViewDistance, binary32; zero when absent. */
	double			view_distance;
	int			has_cue_color;

	/* CueColour, binary32; renderer clamps to 0..255. */
	double			cue_color[3];
	int			has_cue_start;

	/*
	 * CueStart: software cue and halo attenuation, not opaque Glide ramp.
	 */
	double			cue_start;
	int			has_goal_camera;

	/* GoalCamera: eye XYZ, target XYZ, binary32. */
	double			goal_camera[6];
	int			has_ai_goal_wheel;

	/* AIGoalWheel; zero if absent. */
	float			ai_goal_wheel;
	int			has_start_grid;

	/* Row spacing, setback, half-width, odd-slot setback. */
	double			start_grid[4];

	/* Laps, 1..25; zero when absent. */
	unsigned int		laps;

	/* CDTrack, 2..99; zero when absent/disabled. */
	unsigned int		cd_track;

	struct mh_halo_paths	halos;
};

#endif
