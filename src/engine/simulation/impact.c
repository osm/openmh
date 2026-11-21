#include <stddef.h>

#include "engine/core/error.h"
#include "engine/simulation/impact.h"

#define VELOCITY_LIMIT 1e12
#define NORMAL_LIMIT 1.00001
#define SEVERITY_LIMIT 1e15
#define SOUND_CLASS_LIMIT 15

void
mh_impact_begin_frame(struct mh_impact_effects *effects)
{
	if (effects != NULL) {
		effects->contact = 0;
		effects->severity = 0;
	}
}

enum mh_error_code
mh_impact_severity(const double relative_velocity[3], const double normal[3],
	double *output, struct mh_error *error_out)
{
	long double normal_speed;
	unsigned int i;

	if (relative_velocity == NULL || normal == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing impact input");

	for (i = 0; i < 3; i++)
		if (!(relative_velocity[i] >= -VELOCITY_LIMIT &&
			relative_velocity[i] <= VELOCITY_LIMIT) ||
			!(normal[i] >= -NORMAL_LIMIT &&
				normal[i] <= NORMAL_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid impact vector");

	normal_speed =
		(long double)(float)relative_velocity[1] * (float)normal[1] +
		(long double)(float)relative_velocity[0] * (float)normal[0];
	normal_speed +=
		(long double)(float)relative_velocity[2] * (float)normal[2];
	*output = (double)(float)(-2 * normal_speed);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_impact_add(struct mh_impact_effects *effects, unsigned int sound_class,
	double severity, struct mh_error *error_out)
{
	if (effects == NULL || sound_class > SOUND_CLASS_LIMIT ||
		!(severity >= -SEVERITY_LIMIT && severity <= SEVERITY_LIMIT) ||
		(effects->contact != 0 && effects->contact != 1) ||
		effects->sound_class > SOUND_CLASS_LIMIT ||
		!(effects->severity >= 0 &&
			effects->severity <= SEVERITY_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid impact effect");

	severity = (double)(float)severity;
	effects->contact = 1;
	effects->sound_class = sound_class;

	if (severity > effects->severity)
		effects->severity = severity;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
