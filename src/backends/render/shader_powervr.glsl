const float powervr_invz_scale = 0.01;
const float powervr_fixed_depth_scale = 2147483648.0;
const float powervr_fog_density_divisor = 1024.0;
const float powervr_fog_table_size = 128.0;
const float powervr_fog_range = 256.0;
const float powervr_transparency_max = 15.0;
const float powervr_transparency_divisor = 16.0;

/*
 * Series1 default InvZ scale .01, 31-bit depth, density register 10.
 * Reconstruct the 128-entry exponential table algebraically. The native depth
 * interpolator does not emulate historical ISP plane precision.
 */
float
powervr_fog_amount(float reciprocal_w)
{
	float index, shift, table;

	index = floor(reciprocal_w *
		(powervr_invz_scale * powervr_fixed_depth_scale /
			powervr_fog_density_divisor));
	shift = floor(index / powervr_fog_table_size);

	if (shift > 8.0)
		return 0.0;

	table = min(powervr_fog_range - 1.0,
		floor(exp2(-mod(index, powervr_fog_table_size) /
			      powervr_fog_table_size) *
			powervr_fog_range));

	return floor(table * exp2(-shift)) / powervr_fog_range;
}

vec4
powervr_shade(vec4 texel, vec3 vertex_color, vec3 fog_color, bool fog_enabled,
	float reciprocal_w, float primitive_transparency)
{
	vec3 shaded;
	float transparency, alpha;

	shaded = texel.rgb * vertex_color;

	if (fog_enabled)
		shaded = mix(
			shaded, fog_color, powervr_fog_amount(reciprocal_w));

	/*
	 * SGL adds inverted four-bit texture and primitive transparency. Value
	 * 15 means fully transparent.
	 */
	transparency = min(powervr_transparency_max,
		(1.0 - texel.a) * powervr_transparency_max +
			primitive_transparency);
	alpha = transparency >= powervr_transparency_max - 0.001
		? 0.0
		: 1.0 - transparency / powervr_transparency_divisor;

	return vec4(shaded, alpha);
}
