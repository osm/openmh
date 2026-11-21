#ifndef MH_EXTRACT_OUTPUT_OPTIONS_H
#define MH_EXTRACT_OUTPUT_OPTIONS_H

/* Directory retention and optional package media conversion. */
struct mh_extract_options {
	int	keep_original;
	int	compact;
	int	exclude_music;
	int	exclude_movies;
};

#endif
