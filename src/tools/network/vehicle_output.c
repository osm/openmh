#include <stdio.h>

#include "engine/network/protocol.h"
#include "tools/network/vehicle_output.h"

void
mh_tool_print_vehicle(const struct mh_net_vehicle *vehicle)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		printf(" %.17g", vehicle->extension[i]);

	printf(" %.17g %.17g %.17g %.17g %.17g %u", vehicle->steering,
		vehicle->wheel_speed[0], vehicle->wheel_speed[1], vehicle->rpm,
		vehicle->slide, vehicle->gear);

	for (i = 0; i < 4; i++)
		printf(" %.17g", vehicle->auxiliary[i]);

	for (i = 0; i < 4; i++)
		printf(" %u", vehicle->codes[i]);

	printf(" %u %u %u", vehicle->variants[0], vehicle->variants[1],
		vehicle->flags);

	for (i = 0; i < 3; i++)
		printf(" %.17g", vehicle->angles[i]);

	for (i = 0; i < 3; i++)
		printf(" %.17g", vehicle->position[i]);
}
