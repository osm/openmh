#ifndef MH_FONT_FNT_PROVIDER_H
#define MH_FONT_FNT_PROVIDER_H

struct mh_font_source_provider;

/* Process-lifetime source for the original FNT glyph and byte-write maps. */
const struct mh_font_source_provider	*mh_font_fnt_source(void);

#endif
