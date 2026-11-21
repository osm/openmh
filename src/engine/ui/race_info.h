#ifndef MH_UI_RACE_INFO_H
#define MH_UI_RACE_INFO_H

#include "engine/core/error.h"
#include "engine/simulation/field.h"
#include "engine/ui/draw.h"
#include "interfaces/catalog/profile.h"

struct mh_hud_assets;

struct mh_race_info_metadata {
	unsigned long	automatic, control_type;

	/* Full recorded widths plus native terminators. */
	char		name[17], team[6];
};

struct mh_race_info_row {
	/* Stable field index, independent of displayed rank. */
	unsigned int	entry;
	unsigned long	position, color, automatic, control_type, ping;
	char		name[64], team[16];
};

struct mh_race_info {
	char			track[128];
	unsigned int		count;
	struct mh_race_info_row	rows[MH_RACE_MAX_CARS];
};

/* 18 header calls plus 12 per entrant, including ordered black shadows. */
#define MH_RACE_INFO_LINES (18 + 12 * MH_RACE_MAX_CARS)

struct mh_race_info_line {
	char		text[128];
	int		x, y;
	unsigned long	color;

	/* Nonzero for a dashed separator instead of text. */
	unsigned int	separator_width;
};

struct mh_race_info_layout {
	unsigned int			count;
	struct mh_race_info_line	lines[MH_RACE_INFO_LINES];
};

/*
 * Read-only roster snapshot in classification order. Departed players are
 * omitted; unavailable/finished players remain. Network/replay adapters may
 * replace cached metadata without modifying simulation or ranking.
 */
enum mh_error_code	mh_race_info_state(const struct mh_field *field,
	const struct mh_profile *profiles, const unsigned long *control_types,
	struct mh_race_info *output, struct mh_error *error_out);

/* Original centered columns and draw order, with viewport-scaled rules. */
enum mh_error_code	mh_race_info_layout(const struct mh_race_info *info,
	unsigned int width, unsigned int height, unsigned int font_height,
	struct mh_race_info_layout *layout, struct mh_error *error_out);

/* Shared dashed rule drawing; failure restores the list's vertex count. */
int	mh_race_info_draw_separator(const struct mh_race_info_line *line,
	const struct mh_ui_atlas *atlas, const struct mh_ui_region *solid,
	struct mh_ui_list *list);

/*
 * Hardware font: centered columns, all header shadows then white headers, each
 * row's shadows then its player color. Appends to an existing list; failure
 * restores the published count.
 */
int	mh_race_info_draw(const struct mh_hud_assets *assets,
	const struct mh_race_info *info, struct mh_ui_list *list);

#endif
