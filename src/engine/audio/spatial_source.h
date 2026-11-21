#ifndef MH_SPATIAL_SOURCE_H
#define MH_SPATIAL_SOURCE_H

/* Portable game-side data, without device objects or DSP state. */
struct mh_spatial_source_state {
	float	position[3], score;
};

/* has_buffer means a 3D source buffer, distinct from the playback buffer. */
struct mh_spatial_source_input {
	int		spatial, has_buffer;
	unsigned long	sample_rate, priority;
	float		gain, pan, near_distance, far_distance, pitch, elapsed;
	float		position[3];
};

struct mh_spatial_source_parameters {
	float	gain, pan, frequency;
};

struct mh_spatial_source_update {
	int	changed;
	float	position[3], velocity[3];
};

/*
 * DS3D/A3D 1.2 bound-source policy. Both bindings are required for spatial
 * sounds, which retain gain/pan scratch. Nonspatial zero gain is accepted.
 * Source history and output geometry can change before rejection by range.
 */
int	mh_spatial_source_prepare_bound(struct mh_spatial_source_state *state,
	const struct mh_spatial_source_input *input, const float anchor[3],
	int secondary_bound, float master_gain,
	struct mh_spatial_source_parameters *parameters,
	struct mh_spatial_source_update *output);

/* Common mechanics; profile owners choose gates, anchor and voice count. */
void	mh_spatial_source_move(struct mh_spatial_source_state *state,
	const struct mh_spatial_source_input *input,
	struct mh_spatial_source_update *output);
int	mh_spatial_source_in_range(const float position[3],
	const float anchor[3], float near, float far);
void	mh_spatial_source_score(struct mh_spatial_source_state *state,
	unsigned long priority, float master_gain, float *gain);

/*
 * Count must be positive. Return a one-based slot, or zero when rejected.
 * Use the first vacant slot; otherwise replace the earliest strict minimum
 * only when the incoming score is greater.
 */
unsigned int	mh_spatial_source_slot(float score,
	const struct mh_spatial_source_state *const voices[],
	unsigned int count);

#endif
