#include <stddef.h>

#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/contact_schedule.h"

#define PASS_LIMIT 3

enum mh_error_code
mh_contact_schedule(unsigned int count, unsigned int active_mask,
	unsigned int remote_mask, mh_contact_visit visit, void *context,
	struct mh_error *error_out)
{
	unsigned int car, peer, pass;
	int hit, any_hit;
	enum mh_error_code status;

	if (count > MH_RACE_MAX_CARS || active_mask >= (1U << count) ||
		remote_mask >= (1U << count) || visit == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car contact schedule");

	for (car = 0; car < count; car++) {
		if (!(active_mask & (1U << car)) || (remote_mask & (1U << car)))
			continue;

		for (pass = 0; pass < PASS_LIMIT; pass++) {
			hit = 0;
			status = visit(context, MH_CONTACT_TRACK, car, car,
				pass, &hit, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			any_hit = hit;

			for (peer = 0; peer < count; peer++) {
				if (car == peer ||
					!(active_mask & (1U << peer)))
					continue;

				hit = 0;
				status = visit(context, MH_CONTACT_PAIR, car,
					peer, pass, &hit, error_out);

				if (status != MH_ERROR_NONE)
					return status;

				any_hit |= hit;
			}

			if (!any_hit)
				break;
		}

		hit = 0;
		status = visit(context, MH_CONTACT_RESIDUAL, car, car, 0, &hit,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_object_contact_schedule(int sleeping, int *frame_impact,
	mh_object_contact_visit visit, void *context,
	struct mh_error *error_out)
{
	unsigned int pass;
	int had_impact, hit;
	enum mh_error_code status;

	if ((sleeping != 0 && sleeping != 1) || frame_impact == NULL ||
		(*frame_impact != 0 && *frame_impact != 1) || visit == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scenery contact schedule");

	had_impact = *frame_impact;

	if (!sleeping)
		for (pass = 0; pass < PASS_LIMIT; pass++) {
			hit = 0;
			status = visit(context, MH_CONTACT_TRACK, pass, &hit,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (hit)
				had_impact = 1;

			if (!had_impact)
				break;
		}

	if (had_impact) {
		hit = 0;
		status =
			visit(context, MH_CONTACT_RESIDUAL, 0, &hit, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*frame_impact = had_impact;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
