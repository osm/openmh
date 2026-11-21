#ifndef MH_CLIENT_MENUS_COMPACT_H
#define MH_CLIENT_MENUS_COMPACT_H

struct mh_menus;

/* Native selection page for content without a complete retail menu set. */
int	mh_menus_compact(const struct mh_menus *menus);
void	mh_menus_compact_bind(struct mh_menus *menus);
void	mh_menus_compact_choices(struct mh_menus *menus);

#endif
