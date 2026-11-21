#include <string.h>

#include "client/menus/internal.h"
#include "engine/core/error.h"
#include "engine/ui/menu_text.h"

enum {
	RACE_AUDIO_LABEL_X = 190,
	RACE_AUDIO_LABEL_Y = 380,
	RACE_AUDIO_LABEL_COLOR = 1,
	RACE_AUDIO_SELECTED_COLOR = 2
};

static const char *
race_audio_label(unsigned int mode)
{
	switch (mode) {
	case MH_RACE_AUDIO_DS3D:
		return "Race audio: Spatial (DS3D)";

	case MH_RACE_AUDIO_EAX:
		return "Race audio: Spatial (EAX reverb)";

	case MH_RACE_AUDIO_A3D20:
		return "Race audio: Spatial (A3D 2.0)";

	case MH_RACE_AUDIO_A3D12:
		return "Race audio: Spatial (A3D 1.2)";

	default:
		return "Race audio: Software";
	}
}

static void
scan_music_catalog(struct mh_menus *menus)
{
	struct mh_error error;

	if (mh_music_catalog_scan_from(menus->audio_decoder, menus->resources,
		menus->asset_root, menus->music_root, &menus->music_catalog,
		&error) != MH_ERROR_NONE)
		fprintf(stderr, "CD catalog unavailable: %s\n", error.message);
}

static void
read_sound_preferences(struct mh_menus *menus)
{
	menus->sound_values.effects = menus->settings->av.effects_volume;
	menus->sound_values.music = menus->settings->av.music_volume;
	menus->sound_values.loop = menus->settings->av.cd_loop;
	menus->sound_values.menu_track = menus->settings->menu_cd_track;
}

static void
publish_sound_preferences(struct mh_menus *menus)
{
	unsigned int i, source;

	menus->settings->av.effects_volume = menus->sound_values.effects;
	menus->settings->av.music_volume = menus->sound_values.music;
	menus->settings->av.cd_loop = menus->sound_values.loop;

	for (i = 0; i < menus->track_catalog.count; i++) {
		source = menus->track_numbers[i] - 1;
		menus->settings->course_music[source].track =
			menus->course_music[i];
		menus->source_music[source] =
			(unsigned int)menus->course_music[i];
	}
}

static void
apply_sound_events(
	struct mh_menus *menus, const struct mh_menu_sound_output *output)
{
	unsigned int i, kind, value;

	for (i = 0; i < output->count && menus->error.code == MH_ERROR_NONE;
		i++) {
		kind = output->events[i].kind;
		value = output->events[i].value;

		if (kind == MH_MENU_SOUND)
			mh_menus_sound_event(menus, value);
		else if (kind == MH_MENU_PAGE && value == MH_MENU_OPTIONS)
			mh_menus_request(&menus->menu, &menus->options_page);
		else if (kind == MH_MENU_EFFECTS_VOLUME) {
			if (!mh_menu_audio_effects_volume(menus->audio, value))
				mh_error_set(&menus->error, MH_ERROR_BACKEND,
					"cannot set frontend effects volume");
		} else if (kind == MH_MENU_MUSIC_VOLUME) {
			if (!mh_menu_audio_music_volume(menus->audio, value))
				mh_error_set(&menus->error, MH_ERROR_BACKEND,
					"cannot set frontend music volume");
		} else if (kind == MH_MENU_CD_SCAN)
			scan_music_catalog(menus);
		else if (kind == MH_MENU_CD_STOP)
			mh_menu_audio_music_stop(menus->audio);
		else if (kind == MH_MENU_CD_PLAY)
			mh_menu_audio_music_play(menus->audio, value);
	}
}

void
mh_menus_sound_init(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;

	(void)menu;
	menus = (struct mh_menus *)context;
	menus->sound_entries++;
	read_sound_preferences(menus);
	mh_menu_sound_animation_init(&menus->sound_animation);
}

void
mh_menus_sound_draw(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_sound_scene scene;
	const char *label;

	menus = (struct mh_menus *)context;
	scene.names = menus->track_titles;
	scene.tracks = menus->course_music;
	scene.count = menus->track_catalog.count;
	scene.seconds = menus->music_catalog.seconds;
	scene.seconds_count = MH_MUSIC_CATALOG_LAST_TRACK + 1;

	if (menus->error.code == MH_ERROR_NONE)
		(void)mh_menu_sound_render(&menus->sound_art, &menus->font,
			&menus->layout, &menus->sound, &menus->sound_values,
			&scene, menu->position_mix, menus->clock_ms,
			menu->elapsed_ms, 0, &menus->sound_animation,
			&menus->digits, &menus->random_state, &menus->row_noise,
			&menus->work, &menus->error);

	label = race_audio_label(menus->settings->av.race_audio);

	if (menus->error.code == MH_ERROR_NONE && menu->position_mix == 0)
		(void)mh_menu_text_draw(&menus->font,
			(const unsigned char *)label, strlen(label) + 1,
			RACE_AUDIO_LABEL_X, RACE_AUDIO_LABEL_Y,
			menus->sound.selected == MH_MENU_SOUND_MODE
				? RACE_AUDIO_SELECTED_COLOR
				: RACE_AUDIO_LABEL_COLOR,
			&menus->work, &menus->error);
}

void
mh_menus_sound_input(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_sound_catalog catalog;
	struct mh_menu_sound_output output;

	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	read_sound_preferences(menus);
	catalog.tracks = menus->course_music;
	catalog.count = menus->track_catalog.count;
	catalog.last_track = menus->music_catalog.last_track;
	menus->sound_keys.elapsed_ms = menu->elapsed_ms;
	menus->sound_keys.current_track =
		mh_menu_audio_current_track(menus->audio);

	if (mh_menu_sound_update_race_audio(&menus->sound, &menus->sound_values,
		&catalog, &menus->sound_keys,
		&menus->settings->av.race_audio, &output,
		&menus->error) != MH_ERROR_NONE)
		return;

	publish_sound_preferences(menus);

	apply_sound_events(menus, &output);
}

void
mh_menus_sound_leave(struct mh_menu *menu, void *context)
{
	struct mh_menus *menus;
	struct mh_menu_sound_output output;

	(void)menu;
	menus = (struct mh_menus *)context;

	if (menus->error.code != MH_ERROR_NONE)
		return;

	read_sound_preferences(menus);

	if (mh_menu_sound_leave(&menus->sound_values,
		mh_menu_audio_current_track(menus->audio), &output,
		&menus->error) == MH_ERROR_NONE)
		apply_sound_events(menus, &output);
}

enum mh_error_code
mh_menus_open_audio(struct mh_menus *menus, const char *asset_root,
	const char *music_root, int audio_mode, struct mh_error *error_out)
{
	if (menus->settings->av.music_volume != 0)
		scan_music_catalog(menus);

	if (audio_mode == MH_MENUS_AUDIO_DISABLED)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	menus->audio = mh_menu_audio_open_from(menus->audio_provider,
		menus->audio_decoder, asset_root,
		menus->settings->av.effects_volume / (double)MH_AV_VOLUME_MAX,
		menus->resources, menus->timer->source);

	if (menus->audio == NULL && audio_mode == MH_MENUS_AUDIO_REQUIRED)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"frontend audio unavailable");

	mh_menu_audio_music(menus->audio, asset_root, music_root,
		menus->settings->av.music_volume,
		menus->settings->menu_cd_track);

	return mh_menu_audio_load_horns(menus->audio, asset_root, &menus->horns)
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot load frontend horns");
}
