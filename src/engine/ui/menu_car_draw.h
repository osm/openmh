#ifndef MH_MENU_CAR_DRAW_H
#define MH_MENU_CAR_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_car_setup.h"
#include "engine/ui/menu_palette.h"
#include "engine/ui/menu_sprite.h"
#include "engine/ui/menu_thumbnail.h"
#include "interfaces/font/glyphs.h"
#include "interfaces/geometry/wireframe.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_CAR_ART_FRAMES 8

enum mh_menu_car_statistic {
	MH_MENU_CAR_SPEED = 0,
	MH_MENU_CAR_ACCELERATION,
	MH_MENU_CAR_HANDLING,
	MH_MENU_CAR_STATISTICS
};

struct mh_menu_car_art {
	struct mh_sprite_indexed images[MH_MENU_LAYOUT_SLOTS]
				[MH_MENU_CAR_ART_FRAMES];
	unsigned int	count[MH_MENU_LAYOUT_SLOTS];
	unsigned char	palette[MH_MENU_PALETTE_BYTES];
};

struct mh_menu_car_animation {
	struct mh_menu_sprite_state	sprites;
	unsigned int			shown_car;
	float				fade;
	unsigned int			automatic_frame, record_frame;
	float				statistics[MH_MENU_CAR_STATISTICS];

	/* Publish selections after drawing; inherited by Ghost. */
	unsigned int			frames[MH_MENU_LAYOUT_SLOTS];
};

struct mh_menu_car_scene {
	const char			*const *names;
	const char			*const *horn_names;

	/* Car statistic values. */
	const float			(*statistics)[MH_MENU_CAR_STATISTICS];
	const struct mh_menu_thumbnail	*thumbnails;

	const struct mh_wireframe
		*models; /* Already fitted, in original catalog order. */
	struct mh_menu_car_catalog	catalog;
	unsigned int			horn_count, mode, stage;

	/* 64 RGB triples. */
	const unsigned char		*background_palette;
};

enum mh_error_code	mh_menu_car_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_car_art *output, struct mh_error *error_out);
void			mh_menu_car_art_free(struct mh_menu_car_art *art);

/*
 * Reset presentation. Retains shown car, switches and statistic followers. Call
 * mh_menu_car_enter separately for the catalog restriction.
 */
void	mh_menu_car_animation_init(struct mh_menu_car_animation *state);

/*
 * Layout-3 compositor, with supplied catalog and fitted LOBs. The canvas
 * retains padding. All output commits together; invalid data or unsafe drawing
 * returns an error without partial state/pixel writes.
 */
enum mh_error_code	mh_menu_car_render(const struct mh_menu_car_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	const struct mh_menu_car_setup_state *menu,
	const struct mh_menu_car_scene *scene, unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms, int no_subpixel,
	struct mh_menu_car_animation *state, unsigned long *random,
	unsigned long *noise, unsigned char palette[MH_MENU_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
