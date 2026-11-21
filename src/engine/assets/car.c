#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/car.h"
#include "engine/assets/config_line.h"
#include "engine/catalog/halo_paths.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"

#define CONFIG_LINE_BYTES 1024
#define POSITION_LIMIT 10000
#define FORCE_LIMIT 1e9
#define TUNING_LIMIT 1000
#define RPM_LIMIT 1e6
#define ACC_FORCE_LIMIT 1e6
#define MASS_LIMIT 10000
#define GEAR_RATIO_LIMIT 10000
#define BRAKE_FORCE_LIMIT 1000
#define MAX_RPM_MIN 1000
#define STATISTIC_LIMIT 1000000
#define MIN_BRAKE_FORCE 1e-6

#define FIELD_NAME 1
#define FIELD_BODY 2
#define FIELD_SPRING_LENGTH 4
#define FIELD_FIRST_WHEEL 8U
#define FIELD_FIRST_WHEEL_POSITION 128U
#define FIELD_MASS 2048U
#define FIELD_CENTER_MASS 4096U
#define FIELD_SPRING_STRENGTH 8192U
#define FIELD_TURN_FORCE 16384U
#define FIELD_GRIP_LEVEL 32768UL
#define FIELD_BRAKE_FORCE 65536UL
#define FIELD_GEARS 131072UL
#define FIELD_GEAR_RATIOS 262144UL
#define FIELD_MIN_RPM 524288UL
#define FIELD_MAX_RPM 1048576UL
#define FIELD_ACC_FORCE 2097152UL
#define FIELD_ACC_LEVEL 4194304UL
#define FIELD_SPEED_LEVEL 8388608UL
#define FIELD_FIRST_CORNER 16777216UL
#define FIELD_COLLISION 268435456UL
#define FIELD_LINE_OBJECT 536870912UL

#define WHEEL_MODEL_FIELDS                                                     \
	(FIELD_FIRST_WHEEL | (FIELD_FIRST_WHEEL << 1) |                        \
		(FIELD_FIRST_WHEEL << 2) | (FIELD_FIRST_WHEEL << 3))
#define WHEEL_POSITION_FIELDS                                                  \
	(FIELD_FIRST_WHEEL_POSITION | (FIELD_FIRST_WHEEL_POSITION << 1) |      \
		(FIELD_FIRST_WHEEL_POSITION << 2) |                            \
		(FIELD_FIRST_WHEEL_POSITION << 3))
#define ASSEMBLY_FIELDS                                                        \
	(FIELD_NAME | FIELD_BODY | FIELD_SPRING_LENGTH | WHEEL_MODEL_FIELDS |  \
		WHEEL_POSITION_FIELDS)
#define SUSPENSION_FIELDS                                                      \
	(FIELD_MASS | FIELD_CENTER_MASS | FIELD_SPRING_STRENGTH)
#define GRIP_FIELDS (FIELD_GRIP_LEVEL | FIELD_BRAKE_FORCE)
#define ENGINE_FIELDS                                                          \
	(FIELD_GEARS | FIELD_GEAR_RATIOS | FIELD_MIN_RPM | FIELD_MAX_RPM |     \
		FIELD_ACC_FORCE | FIELD_ACC_LEVEL | FIELD_SPEED_LEVEL)
#define CORNER_FIELDS                                                          \
	(FIELD_FIRST_CORNER | (FIELD_FIRST_CORNER << 1) |                      \
		(FIELD_FIRST_CORNER << 2) | (FIELD_FIRST_CORNER << 3))

enum {
	STAT_TOP_SPEED = 1,
	STAT_ACCELERATION = 2,
	STAT_HANDLING = 4,
	ALL_STATISTICS = STAT_TOP_SPEED | STAT_ACCELERATION | STAT_HANDLING,
	ALL_PAINT_KEYS = 7,
	ALL_SHADOW_ROWS = 15
};

struct car_fields {
	unsigned long	present;
	unsigned int	ratio_count, shadows, paint, statistics;
};

static char *
next_token(char **cursor)
{
	char *start;
	char *p;

	p = *cursor;

	while (*p == ' ' || *p == '\t')
		p++;

	start = p;

	while (*p != 0 && *p != ' ' && *p != '\t')
		p++;

	if (*p != 0)
		*p++ = 0;

	*cursor = p;

	return start;
}

static int
copy_path(char *output, const char *value)
{
	size_t length;

	length = strlen(value);

	if (length == 0 || length >= MH_CAR_PATH_SIZE)
		return 0;

	memcpy(output, value, length + 1);

	return 1;
}

static int
read_numbers(char *p, double *values, unsigned int count, double limit)
{
	char *end;
	char *value;
	unsigned int i;

	for (i = 0; i < count; i++) {
		value = next_token(&p);
		errno = 0;
		values[i] = strtod(value, &end);

		/*
		 * Inspection limits also reject infinities and NaNs on
		 * libraries which accept those spellings as an extension to C89
		 * strtod.
		 */
		if (end == value || *end != 0 || errno == ERANGE ||
			!(values[i] >= -limit && values[i] <= limit))
			return 0;
	}

	return *next_token(&p) == 0;
}

static enum mh_error_code
read_line(const unsigned char *text, size_t size, size_t *position,
	char line[CONFIG_LINE_BYTES], struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_config_line_read(
		text, size, position, line, CONFIG_LINE_BYTES, 126, 1);

	if (status == MH_ERROR_UNSUPPORTED)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"configuration contains non-ASCII text");

	if (status == MH_ERROR_RANGE)
		return mh_error_set(
			error_out, status, "configuration line too long");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_paint(
	char *arguments, unsigned long *packed_rgb, struct mh_error *error_out)
{
	char *value, *end;
	long component;
	unsigned int j;

	for (j = 0; j < 3; j++) {
		value = next_token(&arguments);
		errno = 0;
		component = strtol(value, &end, 10);

		if (end == value || *end != 0 || errno == ERANGE ||
			component < MH_I32_MIN || component > MH_I32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid car color integer");

		*packed_rgb =
			(*packed_rgb << 8) | ((unsigned long)component & 255UL);
	}

	if (*next_token(&arguments) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"extra car color component");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_statistic(const char *key, char *arguments, struct mh_car *car,
	struct car_fields *fields, struct mh_error *error_out)
{
	unsigned int flag;
	double number;

	flag = mh_ascii_equal(key, "TopSpeed")	      ? STAT_TOP_SPEED
		: mh_ascii_equal(key, "Acceleration") ? STAT_ACCELERATION
						: STAT_HANDLING;

	if ((fields->statistics & flag) ||
		!read_numbers(arguments, &number, 1, STATISTIC_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or duplicate car statistic");

	fields->statistics |= flag;

	if (flag == STAT_TOP_SPEED)
		car->top_speed = (float)number;
	else if (flag == STAT_ACCELERATION)
		car->acceleration = (float)number;
	else
		car->handling = (float)number;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_geometry(const char *key, char *arguments, struct mh_car *car,
	struct car_fields *fields, unsigned long *field_flag,
	struct mh_error *error_out)
{
	static const char *position_keys[4] = {
		"PhyWheelFL", "PhyWheelFR", "PhyWheelBR", "PhyWheelBL"};

	static const char *corner_keys[4] = {
		"PhyCornerFL", "PhyCornerFR", "PhyCornerBR", "PhyCornerBL"};

	char *value;
	unsigned int i, j;
	double shadow_row[12];

	for (i = 0; i < 4; i++) {
		char wheel_key[7];
		char shadow_key[14];

		strcpy(wheel_key, "Wheel1");
		wheel_key[5] = (char)('1' + i);
		strcpy(shadow_key, "shadowpoints1");
		shadow_key[12] = (char)('1' + i);

		if (mh_ascii_equal(key, shadow_key)) {
			if ((fields->shadows & (1U << i)) ||
				!read_numbers(arguments, shadow_row, 12,
					POSITION_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate shadow row");

			fields->shadows |= 1U << i;

			for (j = 0; j < 12; j++)
				car->shadow_points[i * 4 + j / 3][j % 3] =
					(float)shadow_row[j];
		}

		if (mh_ascii_equal(key, wheel_key)) {
			*field_flag = FIELD_FIRST_WHEEL << i;
			value = next_token(&arguments);

			if (!copy_path(car->wheels[i], value) ||
				*next_token(&arguments) == 0 ||
				*next_token(&arguments) != 0) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Wheel model/texture path");
			}
		} else if (mh_ascii_equal(key, position_keys[i])) {
			*field_flag = FIELD_FIRST_WHEEL_POSITION << i;

			if (!read_numbers(arguments, car->wheel_position[i], 3,
				POSITION_LIMIT)) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhyWheel position");
			}
		} else if (mh_ascii_equal(key, corner_keys[i])) {
			*field_flag = FIELD_FIRST_CORNER << i;

			if (!read_numbers(arguments, car->corners[i], 3,
				POSITION_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhyCorner position");
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_ratios(char *arguments, double ratios[MH_CAR_MAX_GEARS + 1],
	unsigned int *count, struct mh_error *error_out)
{
	char *value;

	*count = 0;

	while (*(value = next_token(&arguments)) != 0) {
		if (*count == MH_CAR_MAX_GEARS + 1 ||
			!read_numbers(
				value, &ratios[*count], 1, GEAR_RATIO_LIMIT) ||
			ratios[*count] < 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid PhyGearRatio");

		(*count)++;
	}

	if (*count < 2)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PhyGearRatio count");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
incomplete_group(unsigned long present, unsigned long mask)
{
	unsigned long group;

	group = present & mask;

	return group != 0 && group != mask;
}

static enum mh_error_code
validate_fields(struct mh_car *car, const struct car_fields *fields,
	struct mh_error *error_out)
{
	if ((fields->present & ASSEMBLY_FIELDS) != ASSEMBLY_FIELDS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing required car assembly field");

	if (fields->paint != 0 && fields->paint != ALL_PAINT_KEYS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete car paint colors");

	if (fields->statistics != 0 && fields->statistics != ALL_STATISTICS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete car display statistics");

	car->has_statistics = fields->statistics == ALL_STATISTICS;
	car->has_paint_keys = fields->paint == ALL_PAINT_KEYS;

	if (incomplete_group(fields->present, SUSPENSION_FIELDS)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete car suspension settings");
	}

	car->has_suspension =
		(fields->present & SUSPENSION_FIELDS) == SUSPENSION_FIELDS;
	car->has_steering = (fields->present & FIELD_TURN_FORCE) != 0;

	if (incomplete_group(fields->present, GRIP_FIELDS))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete car grip settings");

	car->has_grip = (fields->present & GRIP_FIELDS) == GRIP_FIELDS;

	if (incomplete_group(fields->present, ENGINE_FIELDS))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete car engine settings");

	car->has_engine = (fields->present & ENGINE_FIELDS) == ENGINE_FIELDS;

	if (incomplete_group(fields->present, CORNER_FIELDS))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete car body settings");

	car->has_corners = (fields->present & CORNER_FIELDS) == CORNER_FIELDS;

	if (fields->shadows != 0 && fields->shadows != ALL_SHADOW_ROWS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete car shadow outline");

	car->has_shadow = fields->shadows == ALL_SHADOW_ROWS;

	if (car->has_engine &&
		(fields->ratio_count != car->gears + 1 ||
			car->min_rpm >= car->max_rpm)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"inconsistent car engine settings");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_car_config_car_decode(const unsigned char *text, size_t size,
	struct mh_car *car, struct mh_error *error_out)
{
	struct mh_car parsed;
	struct car_fields fields;
	char line[CONFIG_LINE_BYTES];
	char *arguments, *key, *value, *end;
	enum mh_error_code status;
	size_t position;
	unsigned long field_flag;
	unsigned int i;
	double number;

	memset(&parsed, 0, sizeof(parsed));
	memset(&fields, 0, sizeof(fields));
	position = 0;

	while (position < size) {
		status = read_line(text, size, &position, line, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		arguments = line;
		key = next_token(&arguments);

		while (*arguments == ' ' || *arguments == '\t')
			arguments++;

		field_flag = 0;

		if (mh_ascii_equal(key, "CarName")) {
			field_flag = FIELD_NAME;

			if (*arguments == 0 ||
				strlen(arguments) >= sizeof(parsed.name))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid CarName");

			strcpy(parsed.name, arguments);
		} else if (mh_ascii_equal(key, "Halo")) {
			status = mh_halo_paths_append(
				&parsed.halos, arguments, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		} else if (mh_ascii_equal(key, "Division")) {
			value = next_token(&arguments);

			if (parsed.has_division || *value < '0' || *value > '9')
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid or duplicate car Division");

			errno = 0;
			parsed.division = strtoul(value, &end, 10);

			if (errno != 0 || *end != 0 ||
				parsed.division > MH_U32_MAX ||
				*next_token(&arguments) != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid car Division integer");

			parsed.has_division = 1;
		} else if (mh_ascii_equal(key, "FileName")) {
			field_flag = FIELD_BODY;
			value = next_token(&arguments);

			if (!copy_path(parsed.body, value) ||
				*next_token(&arguments) != 0) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid FileName");
			}
		} else if (mh_ascii_equal(key, "LineObjectName")) {
			field_flag = FIELD_LINE_OBJECT;
			value = next_token(&arguments);

			if (!copy_path(parsed.line_object, value) ||
				*next_token(&arguments) != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid LineObjectName");
		} else if (mh_ascii_equal(key, "TopSpeed") ||
			mh_ascii_equal(key, "Acceleration") ||
			mh_ascii_equal(key, "Handling")) {
			status = read_statistic(
				key, arguments, &parsed, &fields, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		} else if (mh_ascii_equal(key, "CollisionName")) {
			field_flag = FIELD_COLLISION;
			value = next_token(&arguments);

			if (!copy_path(parsed.collision_file, value) ||
				*next_token(&arguments) != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid CollisionName");
		} else if (mh_ascii_equal(key, "Colour1") ||
			mh_ascii_equal(key, "Colour2") ||
			mh_ascii_equal(key, "Colour3")) {
			i = (unsigned int)(key[6] - '1');

			if (fields.paint & (1U << i))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate car color");

			status = read_paint(
				arguments, &parsed.paint_keys[i], error_out);

			if (status != MH_ERROR_NONE)
				return status;

			fields.paint |= 1U << i;
		} else if (mh_ascii_equal(key, "PhySpringLength")) {
			field_flag = FIELD_SPRING_LENGTH;

			if (!read_numbers(arguments, &parsed.spring_length, 1,
				POSITION_LIMIT) ||
				parsed.spring_length < 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhySpringLength");
		} else if (mh_ascii_equal(key, "PhyWeight")) {
			field_flag = FIELD_MASS;

			if (!read_numbers(
				arguments, &parsed.mass, 1, MASS_LIMIT) ||
				parsed.mass <= 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhyWeight");
		} else if (mh_ascii_equal(key, "PhyCenterMass")) {
			field_flag = FIELD_CENTER_MASS;

			if (!read_numbers(arguments, parsed.center_mass, 3,
				POSITION_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhyCenterMass");
		} else if (mh_ascii_equal(key, "PhySpringStrength")) {
			field_flag = FIELD_SPRING_STRENGTH;

			if (!read_numbers(arguments, &parsed.spring_strength, 1,
				FORCE_LIMIT) ||
				parsed.spring_strength < 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhySpringStrength");
		} else if (mh_ascii_equal(key, "PhyTurnForce")) {
			field_flag = FIELD_TURN_FORCE;

			if (!read_numbers(arguments, &parsed.turn_force, 1,
				FORCE_LIMIT) ||
				parsed.turn_force < 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhyTurnForce");
		} else if (mh_ascii_equal(key, "GripLevel")) {
			field_flag = FIELD_GRIP_LEVEL;

			if (!read_numbers(
				arguments, &number, 1, TUNING_LIMIT) ||
				number < 0 || number != (unsigned int)number)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid GripLevel");

			parsed.grip_level = (unsigned int)number;
		} else if (mh_ascii_equal(key, "PhyBrakeForce")) {
			field_flag = FIELD_BRAKE_FORCE;

			if (!read_numbers(arguments, &parsed.brake_force, 1,
				BRAKE_FORCE_LIMIT) ||
				parsed.brake_force < MIN_BRAKE_FORCE)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhyBrakeForce");
		} else if (mh_ascii_equal(key, "Gears")) {
			field_flag = FIELD_GEARS;

			if (!read_numbers(
				arguments, &number, 1, MH_CAR_MAX_GEARS) ||
				number < 1 || number != (unsigned int)number)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Gears");

			parsed.gears = (unsigned int)number;
		} else if (mh_ascii_equal(key, "PhyGearRatio")) {
			field_flag = FIELD_GEAR_RATIOS;
			status = read_ratios(arguments, parsed.gear_ratio,
				&fields.ratio_count, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		} else if (mh_ascii_equal(key, "PhyMinRPM")) {
			field_flag = FIELD_MIN_RPM;

			if (!read_numbers(
				arguments, &parsed.min_rpm, 1, RPM_LIMIT) ||
				parsed.min_rpm < 1)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhyMinRPM");
		} else if (mh_ascii_equal(key, "PhyMaxRPM")) {
			field_flag = FIELD_MAX_RPM;

			if (!read_numbers(
				arguments, &parsed.max_rpm, 1, RPM_LIMIT) ||
				parsed.max_rpm < MAX_RPM_MIN)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhyMaxRPM");
		} else if (mh_ascii_equal(key, "PhyAccForce")) {
			field_flag = FIELD_ACC_FORCE;

			if (!read_numbers(arguments, &parsed.acceleration_force,
				1, ACC_FORCE_LIMIT) ||
				parsed.acceleration_force < 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid PhyAccForce");
		} else if (mh_ascii_equal(key, "AccLevel") ||
			mh_ascii_equal(key, "SpeedLevel")) {
			field_flag = mh_ascii_equal(key, "AccLevel")
				? FIELD_ACC_LEVEL
				: FIELD_SPEED_LEVEL;

			if (!read_numbers(
				arguments, &number, 1, TUNING_LIMIT) ||
				number < 0 || number != (unsigned int)number)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid engine tuning level");

			if (field_flag == FIELD_ACC_LEVEL)
				parsed.acceleration_level =
					(unsigned int)number;
			else
				parsed.speed_level = (unsigned int)number;
		} else {
			status = read_geometry(key, arguments, &parsed, &fields,
				&field_flag, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		if ((fields.present & field_flag) != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate car field");

		fields.present |= field_flag;
	}

	status = validate_fields(&parsed, &fields, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*car = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
