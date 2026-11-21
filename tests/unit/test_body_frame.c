#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/body.h"
#include "engine/simulation/drive_internal.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "body frame line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_drive_publication(void)
{
	struct mh_drive drive, saved_drive;
	struct mh_materials materials;

	memset(&drive, 0, sizeof(drive));
	memset(&materials, 0, sizeof(materials));
	materials.count = 1;
	drive.materials = &materials;
	drive.body_sparks.id[0] = 1;
	drive.body_sparks.gain[0] = .5;
	drive.body_sparks.corner[0] = 6;

	CHECK(mh_drive_body_publish(&drive, 1, NULL) == MH_ERROR_NONE);
	CHECK(drive.body_sparks.id[0] == 1 && drive.body_sparks.gain[0] == .5 &&
		drive.body_sparks.corner[0] == 6);

	drive.impact_effects.contact = 1;
	drive.impact_effects.sound_class = 3;
	drive.impact_effects.severity = 99;
	mh_drive_effects_begin_frame(&drive);
	CHECK(drive.body_sparks.id[0] == 0 && drive.body_sparks.gain[0] == .5 &&
		drive.body_sparks.corner[0] == 6);
	CHECK(!drive.impact_effects.contact &&
		drive.impact_effects.sound_class == 3 &&
		drive.impact_effects.severity == 0);

	drive.remote = 1;
	drive.body_sparks.id[0] = 1;
	drive.body_impulse = 123;
	drive.body_effects.contact = 1;
	saved_drive = drive;
	mh_drive_effects_begin_frame(&drive);
	CHECK(memcmp(&drive, &saved_drive, sizeof(drive)) == 0);

	drive.body_effects.present[0] = 1;
	drive.body_effects.effect_material[0] = 1;
	saved_drive = drive;
	CHECK(mh_drive_body_publish(&drive, 1, NULL) != MH_ERROR_NONE &&
		memcmp(&drive, &saved_drive, sizeof(drive)) == 0);
	CHECK(mh_drive_body_publish(&drive, 3, NULL) != MH_ERROR_NONE &&
		memcmp(&drive, &saved_drive, sizeof(drive)) == 0);

	return 0;
}

int
main(void)
{
	struct mh_body_effects effects;
	struct mh_body_feedback feedback, saved;

	memset(&effects, 0, sizeof(effects));
	CHECK(mh_body_effect_impact(&effects, 3, 0, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_body_publish_feedback(&effects, -.6, &feedback, NULL) ==
		MH_ERROR_NONE);
	CHECK(feedback.contact && feedback.strong_air_damping &&
		feedback.material == 3);
	CHECK(mh_body_publish_feedback(&effects, -.5, &feedback, NULL) ==
		MH_ERROR_NONE);
	CHECK(feedback.contact && !feedback.strong_air_damping &&
		feedback.material == 3);
	CHECK(mh_body_effect_contact(&effects, 4, NULL) == MH_ERROR_NONE);
	CHECK(mh_body_publish_feedback(&effects, 1, &feedback, NULL) ==
			MH_ERROR_NONE &&
		feedback.material == 3);
	CHECK(mh_body_effect_impact(&effects, 5, 0, 0, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_body_publish_feedback(&effects, -1, &feedback, NULL) ==
			MH_ERROR_NONE &&
		feedback.material == 5);
	memset(&effects, 0, sizeof(effects));
	CHECK(feedback.contact &&
		feedback.strong_air_damping); /* Reset is not a publication. */
	CHECK(mh_body_publish_feedback(&effects, -1, &feedback, NULL) ==
		MH_ERROR_NONE);
	CHECK(!feedback.contact && !feedback.strong_air_damping &&
		feedback.material == 0);
	memset(&feedback, 0xa5, sizeof(feedback));
	saved = feedback;
	CHECK(mh_body_publish_feedback(&effects, 3, &feedback, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&feedback, &saved, sizeof(feedback)) == 0);
	CHECK(check_drive_publication() == 0);

	effects.contact = 2;
	CHECK(mh_body_publish_feedback(&effects, 1, &feedback, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&feedback, &saved, sizeof(feedback)) == 0);
	puts("frame body feedback, material history, current entry orientation and reset publication passed");

	return 0;
}
