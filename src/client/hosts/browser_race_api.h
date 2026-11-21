#ifndef MH_BROWSER_RACE_API_H
#define MH_BROWSER_RACE_API_H

struct mh_application;
int		 mh_browser_race_attach(struct mh_application *application);
void		 mh_browser_race_detach(void);
int		 mh_browser_race_enable(void);
int		 mh_browser_race_state(void);
int		 mh_browser_race_completed(void);
unsigned int	 mh_browser_race_count(unsigned int kind);
const char	*mh_browser_race_choice(unsigned int kind, unsigned int index);
int		 mh_browser_race_available(unsigned int kind,
	unsigned int index);
const char	*mh_browser_race_control_key(unsigned int profile,
	unsigned int action);
int		 mh_browser_race_bind_key(unsigned int profile,
	unsigned int action, const char *key);
int		 mh_browser_race_reset(void);
int		 mh_browser_race_get(const char *key);
int		 mh_browser_race_set(const char *key, unsigned int value);
const char	*mh_browser_race_player(void);
int		 mh_browser_race_name(const char *name);
const char	*mh_browser_race_team(void);
int		 mh_browser_race_set_team(const char *team);
int		 mh_browser_race_start(void);
int		 mh_browser_race_leave(void);
const char	*mh_browser_race_error(void);
unsigned int	 mh_browser_race_progress(void);
unsigned long	 mh_browser_race_result(unsigned int index, unsigned int field);

double			 mh_browser_race_car_rating(unsigned int id,
	unsigned int field);
const char		*mh_browser_race_track_detail(unsigned int id,
	unsigned int field);
int			 mh_browser_race_map_load(unsigned int id);
unsigned int		 mh_browser_race_map_size(unsigned int dimension);
const unsigned char	*mh_browser_race_map_pixels(void);
unsigned int		 mh_browser_race_resolution_count(
	unsigned int renderer);
unsigned int		 mh_browser_race_resolution(unsigned int renderer,
	unsigned int index, unsigned int dimension);

int	mh_browser_race_name_color(unsigned int index);
int	mh_browser_race_track_music(unsigned int id);

#endif
