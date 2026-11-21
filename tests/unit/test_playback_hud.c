#include <stdio.h>
#include <string.h>

#include "client/session/playback_hud.h"
#include "engine/core/error.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	static struct mh_demo_race race;
	static struct mh_hud_assets assets;
	struct mh_playback_hud hud, reference_hud;
	struct mh_hud_state state;
	struct mh_message_view view;
	unsigned int i;

	for (i = 0; i < 256; i++)
		assets.message_advance[i] = 3;

	race.field.count = 1;
	race.field.playback = 6;
	race.playback.active = 1;
	mh_playback_hud_init(&hud);
	memset(&state, 0, sizeof(state));
	CHECK(mh_playback_hud_frame(&hud, &assets, 960, &race.field, &race,
		NULL, NULL) == MH_ERROR_NONE);
	mh_playback_hud_state(&hud, &state);
	CHECK(state.messages == NULL && state.status_flags == 4);
	race.field.cars[0].ticks = 720;
	CHECK(mh_playback_hud_frame(&hud, &assets, 960, &race.field, &race,
		NULL, NULL) == MH_ERROR_NONE &&
		!hud.has_messages);
	race.field.cars[0].ticks = 750;
	race.playback.anchor_ms = 6008;
	race.notice = 1;
	CHECK(mh_playback_hud_frame(&hud, &assets, 960, &race.field, &race,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(hud.messages.revision == 1 &&
		strcmp(hud.messages.rows[0].text, "Demo play started!") == 0 &&
		hud.messages.rows[0].milliseconds == 6008 &&
		hud.clock_ms == 6250);
	reference_hud = hud;
	CHECK(mh_playback_hud_frame(&hud, &assets, 960, &race.field, &race,
		NULL, NULL) == MH_ERROR_NONE &&
		memcmp(&hud, &reference_hud, sizeof(hud)) == 0);
	race.field.cars[0].ticks = 960;
	race.notice = 2;
	race.notice_ms = 7800;
	race.playback.active = 0;
	CHECK(mh_playback_hud_frame(&hud, &assets, 960, &race.field, &race,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(hud.messages.revision == 2 &&
		strcmp(hud.messages.rows[0].text, "End of demo play!") == 0 &&
		hud.messages.rows[0].milliseconds == 7800 &&
		hud.status_flags == 0);

	/*
	 * CLI collection after both events retains their original event times.
	 */
	mh_playback_hud_init(&reference_hud);
	CHECK(mh_playback_hud_frame(&reference_hud, &assets, 960, &race.field,
		&race, NULL, NULL) == MH_ERROR_NONE &&
		memcmp(&hud, &reference_hud, sizeof(hud)) == 0);
	race.field.cars[0].ticks = 0;
	race.playback.anchor_ms = 0;
	race.notice = 0;
	race.playback.active = 1;
	reference_hud = hud;
	CHECK(mh_playback_hud_frame(&hud, &assets, 960, &race.field, &race,
		NULL, NULL) != MH_ERROR_NONE &&
		memcmp(&hud, &reference_hud, sizeof(hud)) == 0);
	mh_playback_hud_restart(&hud, &race.field);
	CHECK(mh_playback_hud_frame(&hud, &assets, 960, &race.field, &race,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(hud.clock_ms == 8000 && hud.messages.revision == 2 &&
		hud.status_flags == 4);
	CHECK(mh_messages_view(&hud.messages, hud.clock_ms, 480, 12, &view,
		NULL) == MH_ERROR_NONE &&
		view.count == 2 && view.sound);
	race.field.cars[0].ticks = 721;
	race.playback.anchor_ms = 6008;
	race.notice = 1;
	race.field.playback = 3;
	CHECK(mh_playback_hud_frame(&hud, &assets, 320, &race.field, &race,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(hud.messages.revision == 3 && hud.messages.width == 310 &&
		hud.clock_ms == 14008 &&
		hud.messages.rows[0].milliseconds == 14008 &&
		strcmp(hud.messages.rows[0].text, "Ghost play started!") == 0);
	CHECK(mh_messages_view(&hud.messages, hud.clock_ms, 480, 12, &view,
		NULL) == MH_ERROR_NONE &&
		view.count == 1);
	mh_playback_hud_state(&hud, &state);
	CHECK(state.messages == &hud.messages && state.message_ms == 14008 &&
		state.status_flags == 1);
	CHECK(mh_playback_hud_message(&hud, &assets, "Saved picture: ",
		"motor0.tga", 0xe06820UL, NULL) == MH_ERROR_NONE);
	CHECK(hud.messages.revision == 4 &&
		hud.messages.rows[0].milliseconds == 14008 &&
		hud.messages.rows[0].color == 0xe06820UL &&
		strcmp(hud.messages.rows[0].text,
			"Saved picture: motor0.tga") == 0 &&
		strcmp(hud.messages.rows[1].text, "Ghost play started!") == 0);
	reference_hud = hud;
	assets.message_advance[0] = 65536;
	CHECK(mh_playback_hud_message(&hud, &assets, "Invalid", NULL, 0,
		NULL) != MH_ERROR_NONE &&
		memcmp(&hud, &reference_hud, sizeof(hud)) == 0);
	assets.message_advance[0] = 3;
	mh_playback_hud_restart(&hud, &race.field);
	CHECK(hud.messages.revision == 4 && hud.has_messages);

	return failures != 0;
}
