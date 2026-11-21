#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/body.h"
#include "engine/simulation/body_effects.h"

#define IMPULSE_LIMIT 1e15
#define SCRAPE_LIMIT 1e12
#define NORMAL_LIMIT 1.00001
#define FORWARD_LIMIT 1e9
#define UP_LIMIT 2
#define INVERTED_UP_LIMIT -.5
#define LAST_SURFACE_MATERIAL 25
#define SPARK_GAIN_SCALE .03

enum {
	PRIMARY,
	SECONDARY,
	EFFECT_SLOTS
};

static int
valid_effects(const struct mh_body_effects *effects)
{
	unsigned int slot;

	if (effects == NULL ||
		(effects->contact != 0 && effects->contact != 1) ||
		effects->material > MH_U32_MAX ||
		!(effects->impulse >= 0 && effects->impulse <= IMPULSE_LIMIT))
		return 0;

	for (slot = 0; slot < EFFECT_SLOTS; slot++) {
		if (effects->present[slot] != 0 && effects->present[slot] != 1)
			return 0;

		if (!effects->present[slot])
			continue;

		if (effects->effect_material[slot] > MH_U32_MAX ||
			effects->corner[slot] >= MH_CAR_BODY_POINTS ||
			!(effects->scrape_speed[slot] >= -SCRAPE_LIMIT &&
				effects->scrape_speed[slot] <= SCRAPE_LIMIT))
			return 0;
	}

	return 1;
}

enum mh_error_code
mh_body_effect_impact(struct mh_body_effects *effects, unsigned long material,
	unsigned int corner, double impulse, double normal_x, double forward,
	struct mh_error *error_out)
{
	struct mh_body_effects result;
	double scrape_speed, impulse_magnitude;

	if (!valid_effects(effects) || material > MH_U32_MAX ||
		corner >= MH_CAR_BODY_POINTS ||
		!(impulse >= -IMPULSE_LIMIT && impulse <= IMPULSE_LIMIT) ||
		!(normal_x >= -NORMAL_LIMIT && normal_x <= NORMAL_LIMIT) ||
		!(forward >= -FORWARD_LIMIT && forward <= FORWARD_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid body effect impact");

	result = *effects;
	result.contact = 1;
	result.material = material;
	scrape_speed =
		(double)(float)((long double)fabs((double)(float)normal_x) *
			(float)forward);
	impulse_magnitude = fabs((double)(float)impulse);

	if (result.impulse < impulse_magnitude) {
		result.impulse = impulse_magnitude;
		result.present[SECONDARY] = result.present[PRIMARY];
		result.effect_material[SECONDARY] =
			result.effect_material[PRIMARY];
		result.scrape_speed[SECONDARY] = result.scrape_speed[PRIMARY];
		result.corner[SECONDARY] = result.corner[PRIMARY];
		result.present[PRIMARY] = 1;
		result.effect_material[PRIMARY] = material;
		result.scrape_speed[PRIMARY] = scrape_speed;
		result.corner[PRIMARY] = corner;
	} else if (result.scrape_speed[SECONDARY] < scrape_speed) {
		result.present[SECONDARY] = 1;
		result.effect_material[SECONDARY] = material;
		result.scrape_speed[SECONDARY] = scrape_speed;
		result.corner[SECONDARY] = corner;
	}

	*effects = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_effect_contact(struct mh_body_effects *effects, unsigned long material,
	struct mh_error *error_out)
{
	if (!valid_effects(effects) || material > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid body effect contact");

	if (!effects->contact) {
		effects->contact = 1;
		effects->material = material;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_accumulate(const struct mh_body_result *body,
	struct mh_body_effects *effects, struct mh_error *error_out)
{
	struct mh_body_effects result;
	const struct mh_body_impact *impact;
	enum mh_error_code status;
	unsigned int i;

	if (body == NULL || effects == NULL ||
		body->impact_count > MH_BODY_IMPACT_PASSES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid body effect accumulation");

	result = *effects;

	for (i = 0; i < body->impact_count; i++) {
		impact = &body->impacts[i];
		status =
			mh_body_effect_impact(&result, impact->contact.material,
				impact->corner, impact->impulse,
				impact->normal_x, impact->forward, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < MH_CAR_BODY_POINTS; i++)
		if (body->residual[i].hit) {
			status = mh_body_effect_contact(
				&result, body->residual[i].material, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

	*effects = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_publish_feedback(const struct mh_body_effects *effects, double entry_up,
	struct mh_body_feedback *output, struct mh_error *error_out)
{
	struct mh_body_feedback result;

	if (effects == NULL || output == NULL ||
		(effects->contact != 0 && effects->contact != 1) ||
		effects->material > MH_U32_MAX ||
		!(entry_up >= -UP_LIMIT && entry_up <= UP_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid body feedback publication");

	result.contact = effects->contact;
	result.strong_air_damping =
		result.contact && entry_up < INVERTED_UP_LIMIT;
	result.material = result.contact ? effects->material : 0;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_body_sparks_begin_frame(struct mh_body_sparks *sparks)
{
	if (sparks != NULL)
		sparks->id[PRIMARY] = sparks->id[SECONDARY] = 0;
}

enum mh_error_code
mh_body_sparks_update(const struct mh_body_effects *effects,
	const struct mh_materials *materials,
	const struct mh_body_sparks *state, struct mh_body_sparks *output,
	struct mh_error *error_out)
{
	struct mh_body_sparks result;
	unsigned int slot;
	unsigned long material_id;
	long double gain;

	if (!valid_effects(effects) || materials == NULL ||
		materials->count == 0 || materials->count > MH_MATERIAL_LIMIT ||
		state == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid body spark input");

	result = *state;

	for (slot = 0; slot < EFFECT_SLOTS; slot++) {
		if (!effects->present[slot])
			continue;

		material_id = effects->effect_material[slot];

		if (material_id > LAST_SURFACE_MATERIAL)
			material_id = 0;

		if (material_id >= materials->count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid body spark material");

		result.id[slot] = materials->entries[material_id].spark_anim;
		gain = (long double)(float)effects->scrape_speed[slot] *
			SPARK_GAIN_SCALE;
		result.gain[slot] = gain > 1 ? 1 : (double)(float)gain;

		if (result.gain[slot] < 0)
			result.id[slot] = 0;

		result.corner[slot] = effects->corner[slot];
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_body_sparks(const struct mh_body_effects *effects,
	const struct mh_materials *materials, struct mh_body_sparks *output,
	struct mh_error *error_out)
{
	struct mh_body_sparks empty;

	memset(&empty, 0, sizeof(empty));

	return mh_body_sparks_update(
		effects, materials, &empty, output, error_out);
}
