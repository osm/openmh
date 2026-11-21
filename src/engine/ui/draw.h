#ifndef MH_UI_DRAW_H
#define MH_UI_DRAW_H

#include "engine/ui/atlas.h"
#include "interfaces/font/glyphs.h"

#define MH_UI_MAX_QUADS 2048
/* Info, race gap rows, notifications and chat can share one frame. */
#define MH_UI_MAX_TEXTS 256
#define MH_UI_TEXT_BYTES 8192
#define MH_UI_TEXT_LIMIT 1024

enum mh_ui_text_align {
	MH_UI_TEXT_LEFT,
	MH_UI_TEXT_RIGHT,
	MH_UI_TEXT_CENTER
};

/* Original word-font writes, ordered before the specified vertex offset. */
struct mh_ui_word_text {
	const struct mh_font_program	*font;
	unsigned int			before, offset, length, foreground;
	int				x, y;
	enum mh_ui_text_align		align;
};

struct mh_ui_vertex {
	float	position[2], uv[2], color[4];
};

struct mh_ui_list {
	struct mh_ui_vertex	vertices[MH_UI_MAX_QUADS * 6];

	/* Count is vertices, not quads. */
	unsigned int		count, width, height;
	struct mh_ui_word_text	texts[MH_UI_MAX_TEXTS];
	unsigned char		text[MH_UI_TEXT_BYTES];
	unsigned int		text_count, text_size;
};

/* Clipped pixel-coordinate triangles; rejected quads preserve the list. */
void	mh_ui_begin(struct mh_ui_list *list, unsigned int width,
	unsigned int height);
int	mh_ui_quad(struct mh_ui_list *list, const struct mh_ui_atlas *atlas,
	const struct mh_ui_region *region, float x, float y,
	const float color[4]);
int	mh_ui_quad_scaled(struct mh_ui_list *list,
	const struct mh_ui_atlas *atlas, const struct mh_ui_region *region,
	float x, float y, float width, float height, const float color[4]);

/*
 * Copy length non-NUL character bytes; borrow the decoded immutable font until
 * frame submission returns. Foreground is the original packed 16-bit value.
 * Insert at the current vertex count, preserving sprite/text call order.
 * This original software-profile operation is unsupported by the Glide backend.
 * Failure preserves all list counts; empty text adds no command.
 */
enum mh_error_code	mh_ui_word_text(struct mh_ui_list *list,
	const struct mh_font_program *font, const unsigned char *text,
	size_t length, int x, int y, unsigned int foreground,
	enum mh_ui_text_align align, struct mh_error *error_out);

#endif
