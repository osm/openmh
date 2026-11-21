#ifndef MH_SOFTWARE_MATERIAL_H
#define MH_SOFTWARE_MATERIAL_H

/* Source MYO flag byte, distinct from the generated software face flags. */
#define MH_SOFTWARE_MATERIAL_PERSPECTIVE 1U
#define MH_SOFTWARE_MATERIAL_CUTOUT 2U
/* Preserve the original bit names where only the depth-band use is known. */
#define MH_SOFTWARE_MATERIAL_DEPTH4 4U
#define MH_SOFTWARE_MATERIAL_DEPTH128 128U
#define MH_SOFTWARE_MATERIAL_DEPTH_MASK                                        \
	(MH_SOFTWARE_MATERIAL_DEPTH4 | MH_SOFTWARE_MATERIAL_DEPTH128)
#define MH_SOFTWARE_MATERIAL_EMISSIVE 16U
#define MH_SOFTWARE_MATERIAL_ENVIRONMENT_MASK 0x60U
#define MH_SOFTWARE_MATERIAL_FLAG_MASK 255U

/* Authored material brightness before fog and channel saturation. */
#define MH_SOFTWARE_BRIGHTNESS_MAX 200

#endif
