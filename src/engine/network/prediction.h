#ifndef MH_NET_PREDICTION_H
#define MH_NET_PREDICTION_H

#define MH_NET_PREDICTION_MIN_DEFAULT 1
#define MH_NET_PREDICTION_MAX_DEFAULT 5
#define MH_NET_PREDICTION_LIMIT 10
#define MH_NET_PREDICTION_COMMAND_LIMIT 9

/* Local playback window in snapshot indices; never sent over the network. */
struct mh_net_prediction {
	unsigned int	minimum, maximum;
};

#endif
