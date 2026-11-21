/*
 * Test backend bundle: native libraries exist, but features can be unavailable.
 */
#include <stdio.h>
#include <stdlib.h>

#include "client/hosts/capabilities.h"

int
mh_capability_available(enum mh_capability capability)
{
	static const char *names[] = {
		"network", "movies", "clipboard", "screenshots"};

	static unsigned int reported;
	const char *text;
	unsigned int bit, allowed;
	int available;

	if ((unsigned int)capability >= sizeof(names) / sizeof(names[0]))
		return 0;

	text = getenv("MH_TEST_CAPABILITIES");
	allowed = text == NULL ? 0 : (unsigned int)strtoul(text, NULL, 10);
	bit = 1U << (unsigned int)capability;
	available = (allowed & bit) != 0;

	if (!(reported & bit)) {
		fprintf(stderr, "Test capability %s: %s\n", names[capability],
			available ? "available" : "unavailable");
		reported |= bit;
	}

	return available;
}
