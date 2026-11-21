#ifndef MH_BODY_EFFECTS_H
#define MH_BODY_EFFECTS_H

#include "engine/core/error.h"
#include "interfaces/material/material.h"

struct mh_body_result;
struct mh_body_feedback;

/*
 * Impact maximum and two effect records.
 * Zero initialization clears a frame. Strictly stronger impulses replace
 * the primary record and move its previous value to secondary.
 */
struct mh_body_effects {
	int		contact, present[2];
	unsigned long	material, effect_material[2];
	unsigned int	corner[2];
	double		impulse, scrape_speed[2];
};

struct mh_body_sparks {
	/* SparkAnimID, also the original scratch audio gate. */
	unsigned long	id[2];
	double		gain[2];
	unsigned int	corner[2];
};

/*
 * normal_x is the impact's local normal before rotation; forward is local Z
 * velocity after that impact's rotation/rebasing.
 */
enum mh_error_code	mh_body_effect_impact(struct mh_body_effects *effects,
	unsigned long material, unsigned int corner, double impulse,
	double normal_x, double forward, struct mh_error *error_out);

/* A residual hit supplies the material only if no preceding hit did. */
enum mh_error_code	mh_body_effect_contact(struct mh_body_effects *effects,
	unsigned long material, struct mh_error *error_out);

/* Original binary32 gains; no allocation or rendering. */
enum mh_error_code	mh_body_sparks(const struct mh_body_effects *effects,
	const struct mh_materials *materials, struct mh_body_sparks *output,
	struct mh_error *error_out);

/*
 * Clear spark IDs at frame entry; gains/corners survive. Missing effect records
 * leave the publication untouched. State/output may alias.
 */
void			mh_body_sparks_begin_frame(
	struct mh_body_sparks *sparks);
enum mh_error_code	mh_body_sparks_update(
	const struct mh_body_effects *effects,
	const struct mh_materials *materials,
	const struct mh_body_sparks *state, struct mh_body_sparks *output,
	struct mh_error *error_out);

/*
 * Accumulate car impact records and residual material into a frame.
 * Caller resets once per rendered frame, rather than once per physics slice.
 */
enum mh_error_code	mh_body_accumulate(const struct mh_body_result *body,
	struct mh_body_effects *effects, struct mh_error *error_out);

/*
 * Publish frame contact/material after each slice. The orientation is THIS
 * slice's entry up-Y, even when its contact was earlier. Reset effects at frame
 * start, but retain the previous publication until the first slice finishes:
 * the road/grip stage still consumes that value.
 */
enum mh_error_code	mh_body_publish_feedback(
	const struct mh_body_effects *effects, double entry_up,
	struct mh_body_feedback *output, struct mh_error *error_out);

#endif
