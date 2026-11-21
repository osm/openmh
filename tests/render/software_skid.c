#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/render/software_skid.h"

/* Start, end and normal, each with three components. */

int
main(void)
{
	struct mh_skid_segment segment;
	struct mh_software_skid skid, saved;
	float left[3], right[3];

	memset(&segment, 0, sizeof(segment));
	segment.normal[1] = 1;
	segment.end[2] = 2.5f;

	if (mh_software_skid_prepare(&segment, &skid, NULL) != MH_ERROR_NONE ||
		skid.quads != 1 || skid.sample[2] != .5f)
		return 1;

	mh_software_skid_pair(&skid, 0, left, right);

	if (left[0] != -.1f || right[0] != .1f || left[1] != 0 ||
		right[1] != 0 || left[2] != -.02f || right[2] != -.02f)
		return 1;

	segment.end[2] = 3;

	if (mh_software_skid_prepare(&segment, &skid, NULL) != MH_ERROR_NONE ||
		skid.quads != 3)
		return 1;

	mh_software_skid_pair(&skid, 1, left, right);

	if (left[2] != 1 || right[2] != 1)
		return 1;

	saved = skid;
	segment.end[2] = FLT_MAX;

	if (mh_software_skid_prepare(&segment, &skid, NULL) !=
			MH_ERROR_INVALID ||
		memcmp(&skid, &saved, sizeof(skid)) != 0)
		return 1;

	segment.end[2] = 0;

	if (mh_software_skid_prepare(&segment, &skid, NULL) != MH_ERROR_NONE ||
		skid.quads != 0)
		return 1;

	segment.end[1] = 1;

	if (mh_software_skid_prepare(&segment, &skid, NULL) != MH_ERROR_NONE ||
		skid.quads != 0)
		return 1;

	/*
	 * The width product retains extended precision before its float store.
	 */
	segment.end[1] = 0;
	segment.end[0] = 1e-7f;
	segment.normal[0] = .25f;
	segment.normal[1] = .9f;
	segment.normal[2] = -.4f;

	if (mh_software_skid_prepare(&segment, &skid, NULL) != MH_ERROR_NONE ||
		skid.quads != 1)
		return 1;

	mh_software_skid_pair(&skid, 0, left, right);

	if (left[2] != .091381147503852844f ||
		right[2] != -.091381147503852844f)
		return 1;

	return 0;
}
