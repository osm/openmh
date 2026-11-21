#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_halo_geometry.h"

static int
geometry(void)
{
	struct mh_software_halo_source source;
	struct mh_software_halo_geometry output, saved;
	struct mh_software_fog fog;
	struct mh_software_depth_band band;
	struct mh_projection projection;
	struct mh_indexed_image glow, lens;
	unsigned int i;

	memset(&source, 0, sizeof(source));
	memset(&glow, 0, sizeof(glow));
	memset(&lens, 0, sizeof(lens));
	glow.width = glow.height = 170;
	lens.width = lens.height = 32;
	source.flags = MH_SOFTWARE_HALO_ENABLED | MH_SOFTWARE_HALO_LENSES |
		MH_SOFTWARE_HALO_WORLD;
	source.intensity = source.size = 1;
	source.flare = .65f;
	memset(source.color, 100, sizeof(source.color));

	for (i = 0; i < 2; i++) {
		source.endpoints[i].value[0] = 160;
		source.endpoints[i].value[1] = 100;
		source.endpoints[i].value[2] = 100;
		source.endpoints[i].value[3] = .01f;
	}

	band.offset = .005f;
	band.scale = .99f;

	if (mh_projection_setup(320, 200, 1.6f, 1.2f, 1, 1000, &projection,
		NULL) != MH_ERROR_NONE ||
		mh_software_fog_setup(1, 1000, .7f, &fog, NULL) !=
			MH_ERROR_NONE ||
		mh_software_halo_geometry(&source, &projection, &fog, &band,
			&glow, &lens, 1, &output, NULL) != MH_ERROR_NONE ||
		output.count != 8 || output.images[0] != &glow ||
		output.images[7] != &lens || output.marker[0] != 160)
		return 0;

	saved = output;
	glow.width = 0;

	if (mh_software_halo_geometry(&source, &projection, &fog, &band, &glow,
		&lens, 1, &output, NULL) != MH_ERROR_INVALID ||
		memcmp(&saved, &output, sizeof(output)) != 0)
		return 0;

	glow.width = 170;
	source.endpoints[0].outcode = MH_CLIP_NEAR;

	return mh_software_halo_geometry(&source, &projection, &fog, &band,
		&glow, &lens, 1, &output, NULL) == MH_ERROR_NONE &&
		output.count == 0;
}

int
main(void)
{
	return geometry() ? EXIT_SUCCESS : EXIT_FAILURE;
}
