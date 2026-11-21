#ifndef MH_GPU_UNIFORMS_H
#define MH_GPU_UNIFORMS_H

/*
 * Shader Camera/Material blocks: offsets and sizes are in floats, not bytes.
 * Camera starts with the combined matrix at offset zero. Keep these layouts
 * synchronized with the shader blocks.
 */
enum {
	MH_GPU_CAMERA_VIEW = 16,
	MH_GPU_CAMERA_MODEL = 32,
	MH_GPU_CAMERA_ASPECT = 48,
	MH_GPU_CAMERA_WIDTH = 49,
	MH_GPU_CAMERA_HEIGHT = 50,
	MH_GPU_CAMERA_POWERVR = 51,
	MH_GPU_CAMERA_FOG = 52,
	MH_GPU_CAMERA_INVERSE_FAR = 55,
	MH_GPU_CAMERA_WATER_PHASE = 56,
	MH_GPU_CAMERA_WATER_SCALE = 57,
	MH_GPU_CAMERA_WATER_FREQUENCY = 58,
	MH_GPU_CAMERA_WATER_ENABLED = 59,
	MH_GPU_CAMERA_FLOATS = 60,
	MH_GPU_MATERIAL_ENVIRONMENT = 0,
	MH_GPU_MATERIAL_PRELIT = 1,
	MH_GPU_MATERIAL_FOG = 2,
	MH_GPU_MATERIAL_ALPHA = 3,
	MH_GPU_MATERIAL_DISTANT = 4,
	MH_GPU_MATERIAL_PREPARED = 5,
	MH_GPU_MATERIAL_FLAT = 6,
	MH_GPU_MATERIAL_OPACITY = 7,
	MH_GPU_MATERIAL_FLOATS = 8
};

/* Particle effects bypass the ordinary texture alpha test. */
#define MH_GPU_ALPHA_PARTICLE 2.0f

struct mh_material_policy;

/* Pack portable material decisions into the shared shader ABI. */
void	mh_gpu_material_uniforms(const struct mh_material_policy *policy,
	float output[MH_GPU_MATERIAL_FLOATS]);

#endif
