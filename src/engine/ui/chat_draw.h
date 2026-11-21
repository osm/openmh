#ifndef MH_UI_CHAT_DRAW_H
#define MH_UI_CHAT_DRAW_H

struct mh_hud_assets;
struct mh_ui_list;

/*
 * White editable suffix at the bottom left, cropped from the left to min(view
 * width-15,540). Cursor shows at clock%1000 >= 500. NULL text is inactive; an
 * empty string still has the blinking cursor. Uses hardware myfont advances,
 * independently of the FONT2 editor limit. Appends atomically; no background,
 * shadow or viewport scaling.
 */
int	mh_chat_draw(const struct mh_hud_assets *assets, const char *text,
	unsigned long milliseconds, struct mh_ui_list *list);

#endif
