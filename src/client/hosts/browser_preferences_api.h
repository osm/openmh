#ifndef MH_BROWSER_PREFERENCES_API_H
#define MH_BROWSER_PREFERENCES_API_H

struct mh_application;
void		 mh_browser_preferences_attach(struct mh_application *app);
const char	*mh_browser_preferences_error(void);
unsigned int	 mh_browser_preferences_count(void);
const char	*mh_browser_preferences_text(unsigned int index,
	unsigned int field);
double		 mh_browser_preferences_number(unsigned int index,
	unsigned int field);
int		 mh_browser_preferences_begin(unsigned int count);
int		 mh_browser_preferences_set(unsigned int index, double number,
	const char *text);
int		 mh_browser_preferences_commit(void);
const char	*mh_browser_controls_key(unsigned int scan);
double		 mh_browser_controls_get(unsigned int profile,
	unsigned int field);
int		 mh_browser_controls_begin(unsigned int profile);
int		 mh_browser_controls_set(unsigned int field, double value);
int		 mh_browser_controls_commit(void);

#endif
