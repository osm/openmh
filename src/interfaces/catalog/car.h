#ifndef MH_CATALOG_CAR_H
#define MH_CATALOG_CAR_H

#include <stddef.h>

#include "interfaces/catalog/halo_paths.h"

#define MH_CAR_PATH_SIZE 256
#define MH_CAR_MAX_GEARS 6
#define MH_CAR_PAINT_KEYS 3

/*
 * Decoded assembly and vehicle-physics definition. Wheel order is front
 * left, front right, back right, back left. Paths are logical asset names,
 * not host filesystem paths. The value owns its inline names and arrays.
 */
struct mh_car {
	char			name[128];
	int			has_division;

	/* CAR Division, used by menus. */
	unsigned long		division;
	char			body[MH_CAR_PATH_SIZE];

	/* Optional LineObjectName. */
	char			line_object[MH_CAR_PATH_SIZE];

	/* Complete TopSpeed/Acceleration/Handling display group. */
	int			has_statistics;

	/* Display statistics, separate from physics tuning. */
	float			top_speed, acceleration, handling;

	/* Optional CollisionName; driving contacts require it. */
	char			collision_file[MH_CAR_PATH_SIZE];
	char			wheels[4][MH_CAR_PATH_SIZE];
	int			has_paint_keys;

	/* Colour1..3 replacement keys, packed as 0xRRGGBB. */
	unsigned long		paint_keys[MH_CAR_PAINT_KEYS];
	double			wheel_position[4][3];
	double			spring_length;

	/* Weight, center of mass and spring strength all present. */
	int			has_suspension;
	double			mass;
	double			center_mass[3];
	double			spring_strength;

	/* PhyTurnForce present, independently of the suspension group. */
	int			has_steering;
	double			turn_force;

	/* GripLevel and PhyBrakeForce both present. */
	int			has_grip;

	/* Runtime clamps levels above ten. */
	unsigned int		grip_level;
	double			brake_force;
	int			has_engine;

	/* Forward gears; ratio index zero is reverse. */
	unsigned int		gears;
	double			gear_ratio[MH_CAR_MAX_GEARS + 1];
	double			min_rpm, max_rpm, acceleration_force;
	unsigned int		acceleration_level, speed_level;

	/* All four PhyCorner fields present, FL FR BR BL. */
	int			has_corners;

	/* Stored Y is preserved; runtime uses center_mass Y. */
	double			corners[4][3];

	/* All four ShadowPoints rows present. */
	int			has_shadow;

	/* ShadowPoints1..4: four rows of four XYZs. */
	float			shadow_points[16][3];

	struct mh_halo_paths	halos;
};

#endif
