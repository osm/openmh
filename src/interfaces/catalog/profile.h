#ifndef MH_CATALOG_PROFILE_H
#define MH_CATALOG_PROFILE_H

#define MH_PROFILE_CATALOG_LIMIT 128
#define MH_PROFILE_REFERENCE_BYTES 256
#define MH_PROFILE_RGB_MASK 0xffffffUL

/* Presence bits: PlayerColour followed by CarColour1..3. */
enum mh_profile_color_fields {
	MH_PROFILE_BODY_COLORS = 14,
	MH_PROFILE_ALL_COLORS = 15
};

enum mh_profile_league_fields {
	MH_PROFILE_CAR_FIELDS = 15,
	MH_PROFILE_HORN_FIELD = 16,
	MH_PROFILE_LEAGUE_FIELDS = MH_PROFILE_CAR_FIELDS | MH_PROFILE_HORN_FIELD
};

struct mh_profile {
	char		name[64], nick[16], picture[64];
	char		cars[4][32], horn[16];

	/* PlayerColour, CarColour1..3 as 0xRRGGBB. */
	unsigned long	colors[4];
	unsigned int	color_fields; /* Presence bits in the same order. */

	/* Default .5; parsed values normalized/clamped to 0..1. */
	double		aggressiveness, eagerness;

	/* Bit 0 Aggressiveness, bit 1 Eagerness. */
	unsigned int	tuning_fields;

	/* Bits 0..3 cars, bit 4 horn; absent stays empty. */
	unsigned int	league_fields;

	/* Runtime human/network team, separate from ADP PlayerNick. */
	char		team[16];
};

struct mh_profile_catalog {
	unsigned int		count;
	struct mh_profile	profiles[MH_PROFILE_CATALOG_LIMIT];
	char			files[MH_PROFILE_CATALOG_LIMIT][MH_PROFILE_REFERENCE_BYTES];
};

#endif
