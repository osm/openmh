#include <stddef.h>
#include <string.h>

#include "client/application/race_frontend_internal.h"
#include "client/settings/render_choice.h"
#include "engine/game/race_limits.h"
#include "engine/render/viewport.h"

struct request_field {
	const char	*name;
	size_t		offset;
	unsigned int	minimum, maximum;
};

#define FIELD(name, low, high)                                                 \
	{#name, offsetof(struct mh_race_request, name), low, high}
static const struct request_field fields[] = {FIELD(player_color, 0, 0xffffff),
	FIELD(car, 1, MH_CAR_CATALOG_LIMIT),
	FIELD(track, 1, MH_TRACK_CATALOG_LIMIT), FIELD(mode, 0, 2),
	FIELD(laps, MH_RACE_MIN_LAPS, MH_RACE_MAX_LAPS),
	FIELD(difficulty, 0, 2), FIELD(automatic, 0, 1), FIELD(catch_up, 0, 1),
	FIELD(renderer, 0, MH_RENDER_CHOICE_COUNT - 1),
	FIELD(width, MH_VIEW_MIN_WIDTH, MH_VIEW_MAX_WIDTH),
	FIELD(height, MH_VIEW_MIN_HEIGHT, MH_VIEW_MAX_HEIGHT),
	FIELD(detail, 0, MH_DETAIL_CUSTOM), FIELD(show_fps, 0, 1),
	FIELD(show_player_names, 0, 1), FIELD(effects_volume, 0, 100),
	FIELD(music_volume, 0, 100),
	FIELD(audio_profile, 0, MH_RACE_AUDIO_A3D20), FIELD(controls, 0, 4095),
	FIELD(horn, 0, MH_HORNS - 1),
	FIELD(music_track, 0, MH_MUSIC_CATALOG_LAST_TRACK)};
#undef FIELD

static const struct request_field *
find_field(const char *name)
{
	unsigned int i;

	if (name == NULL)
		return NULL;

	for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
		if (strcmp(name, fields[i].name) == 0)
			return &fields[i];

	return NULL;
}

static enum mh_error_code
validate_value(const struct request_field *field, unsigned int value,
	struct mh_error *error_out)
{
	if (value < field->minimum || value > field->maximum)
		return mh_error_set(error_out, MH_ERROR_RANGE, field->name);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_race_request_get(const struct mh_race_request *request, const char *name)
{
	const struct request_field *field;

	field = find_field(name);

	if (request == NULL || field == NULL)
		return -1;

	return (int)*(
		const unsigned int *)((const char *)request + field->offset);
}

enum mh_error_code
mh_race_request_set(struct mh_race_request *request, const char *name,
	unsigned int value, struct mh_error *error_out)
{
	const struct request_field *field;
	enum mh_error_code status;

	field = find_field(name);

	if (request == NULL || field == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unknown race setting");

	status = validate_value(field, value, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*(unsigned int *)((char *)request + field->offset) = value;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_race_frontend_defaults(
	const struct mh_application *app, struct mh_race_request *r)
{
	const struct mh_settings *s;

	s = app->preferences;
	memset(r, 0, sizeof(*r));
	memcpy(r->name, s->player_name, sizeof(r->name));
	memcpy(r->team, s->team_name, sizeof(r->team));
	r->player_color = (unsigned int)s->player_color;
	r->horn = s->horn;
	r->car = s->car + 1;
	r->track = s->track + 1;
	r->mode = s->game_type == 2 ? 2 : 0;
	r->laps = s->laps;
	r->difficulty = s->difficulty;
	r->automatic = s->automatic;
	r->catch_up = s->catch_up;
	r->renderer = s->renderer;
	r->width = (unsigned int)s->av.width;
	r->height = (unsigned int)s->av.height;
	r->detail = s->av.detail_mode;
	r->show_fps = s->av.show_fps;
	r->show_player_names = s->av.name_plates != MH_AV_NAMES_NONE;
	r->effects_volume = (s->av.effects_volume * 100 + 127) / 255;
	r->music_volume = (s->av.music_volume * 100 + 127) / 255;
	r->audio_profile = s->av.race_audio;
	r->controls = app->race_frontend->controls.selected;
}

static int
available(const struct mh_application *app, enum mh_frontend_catalog kind,
	unsigned int index)
{
	int enabled;

	return mh_application_race_choice(app, kind, index, &enabled) != NULL &&
		enabled;
}

static enum mh_error_code
validate_player_text(
	const struct mh_race_request *r, struct mh_error *error_out)
{
	size_t i;

	for (i = 0; i < sizeof(r->name) && r->name[i] != 0; i++)

		if ((unsigned char)r->name[i] < 32 ||
			(unsigned char)r->name[i] > 126)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"player name must use printable ASCII");

	if (i == 0 || i == sizeof(r->name))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"player name must contain 1 to 15 characters");

	for (i = 0; i < sizeof(r->team) && r->team[i] != 0; i++)
		if ((unsigned char)r->team[i] < 32 ||
			(unsigned char)r->team[i] > 126)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"team must use printable ASCII");

	if (i == sizeof(r->team))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"team must contain at most 4 characters");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_request(struct mh_application *app, const struct mh_race_request *r,
	struct mh_error *error_out)
{
	unsigned int i, value;
	enum mh_error_code status;

	if (r == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing race request");

	for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		value = *(const unsigned int *)((const char *)r +
			fields[i].offset);
		status = validate_value(&fields[i], value, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = validate_player_text(r, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!available(app, MH_FRONTEND_CARS, r->car - 1) ||
		!available(app, MH_FRONTEND_TRACKS, r->track - 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"car or track is unavailable");

	if (!available(app, MH_FRONTEND_AUDIO_PROFILES, r->audio_profile) ||
		!available(app, MH_FRONTEND_RENDERERS, r->renderer) ||
		!available(app, MH_FRONTEND_MODES, r->mode) ||
		!available(app, MH_FRONTEND_CONTROLS, r->controls) ||
		!available(app, MH_FRONTEND_HORNS, r->horn))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"audio profile, renderer, mode, controls or horn are unavailable");

	if (r->music_track != 0 &&
		(r->music_track < 2 ||
			!available(app, MH_FRONTEND_MUSIC, r->music_track - 2)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"music track is unavailable");

	if (r->renderer == MH_RENDER_CHOICE_SOFTWARE &&
		r->width > MH_SOFTWARE_VIEW_MAX_WIDTH)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software rendering supports widths up to 1023");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_frontend_configure(struct mh_application *app,
	const struct mh_race_request *r, struct mh_settings *output,
	struct mh_error *error_out)
{
	struct mh_settings settings;
	enum mh_error_code status;

	status = validate_request(app, r, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	settings = *app->preferences;
	strcpy(settings.player_name, r->name);
	strcpy(settings.team_name, r->team);
	settings.player_color = r->player_color;
	settings.horn = r->horn;
	settings.car = r->car - 1;
	settings.track = r->track - 1;

	if (r->music_track != 0)
		settings.course_music[settings.track].track = r->music_track;

	settings.game_type = r->mode;
	settings.laps = r->laps;
	settings.difficulty = r->difficulty;
	settings.automatic = r->automatic;
	settings.catch_up = r->catch_up;
	settings.renderer = r->renderer;
	settings.av.width = r->width;
	settings.av.height = r->height;
	settings.av.show_fps = r->show_fps;

	if (settings.av.detail_mode != r->detail) {
		settings.av.detail_mode = r->detail;
		status = mh_av_settings_preset(&settings.av, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	/* Apply visibility after presets, preserving an enabled plate style. */
	if (!r->show_player_names)
		settings.av.name_plates = MH_AV_NAMES_NONE;
	else if (settings.av.name_plates == MH_AV_NAMES_NONE)
		settings.av.name_plates = MH_AV_NAMES_TRANSPARENT;

	settings.av.effects_volume = (r->effects_volume * 255 + 50) / 100;
	settings.av.music_volume = (r->music_volume * 255 + 50) / 100;
	settings.av.race_audio = r->audio_profile;
	strcpy(settings.control_name,
		app->race_frontend->controls.profiles[r->controls].layout.name);
	*output = settings;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
