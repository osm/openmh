#ifndef MH_TOOL_NET_VEHICLE_OUTPUT_H
#define MH_TOOL_NET_VEHICLE_OUTPUT_H

struct mh_net_vehicle;

/* Diagnostic channels with leading spaces; caller owns prefix and newline. */
void	mh_tool_print_vehicle(const struct mh_net_vehicle *vehicle);

#endif
