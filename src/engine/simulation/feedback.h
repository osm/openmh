#ifndef MH_SIM_FEEDBACK_H
#define MH_SIM_FEEDBACK_H

#define MH_FEEDBACK_MAX_FORCE 10000

struct mh_field;

enum mh_feedback_effect {
	MH_FEEDBACK_IMPACT,
	MH_FEEDBACK_ROAD,
	MH_FEEDBACK_SPRING,
	MH_FEEDBACK_EFFECTS
};

struct mh_feedback_status {
	int	available, playing, emulated, valid;
};

struct mh_feedback_state {
	float		timer[MH_FEEDBACK_EFFECTS];
	int		restart;
	long		impact_magnitude;

	/* Road period is in microseconds. */
	unsigned long	road_magnitude, road_period;
	long		spring_strength, spring_deadband;
};

struct mh_feedback_input {
	int				enabled;
	struct mh_feedback_status	effects[MH_FEEDBACK_EFFECTS];
	int				sliding, wheelspin;
	int				front_contact[2];

	/* Material eligibility is separate from physical wheel support. */
	int				front_surface[2];
	float				front_bumpy[2];

	/* Local velocity X/Z and the grip stage's slide amount. */
	float				lateral, forward, slide;

	/*
	 * Frame impact flags; track contact takes precedence over pair contact.
	 */
	int				track_hit, pair_hit;
	float	track_impulse, track_normal[3], pair_impulse[3];
};

struct mh_feedback_update {
	int	changed[MH_FEEDBACK_EFFECTS], start[MH_FEEDBACK_EFFECTS];

	/* Whether the original would consult each effect's playback status. */
	int	query[MH_FEEDBACK_EFFECTS];
	long	direction[2];

	/*
	 * Original shared pair vector after the impact consumer's X/Z scaling.
	 */
	float	pair_impulse[3];
};

/* Cold-start timers and creation parameters; no device I/O. */
void	mh_feedback_reset(struct mh_feedback_state *state);

/* Fill physics inputs from local car zero, retaining caller's device status. */
void	mh_feedback_sample(const struct mh_field *field,
	struct mh_feedback_input *input);

/*
 * Once per race frame, using supplied binary32 inputs. Track normal is already
 * transformed into the final car-local basis. Finite simulation values and
 * nonnegative elapsed time are required. Negative retained timers retain
 * immediate-update behavior. No allocation, OS types or device calls. The
 * caller owns enable/stop and autocenter policy. State must be reset before
 * first use; output is cleared on every call.
 */
void	mh_feedback_step(struct mh_feedback_state *state,
	const struct mh_feedback_input *input, float elapsed,
	struct mh_feedback_update *output);

#endif
