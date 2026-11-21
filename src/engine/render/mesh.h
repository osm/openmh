#ifndef MH_RENDER_MESH_H
#define MH_RENDER_MESH_H

#include "engine/animation/scene.h"
#include "engine/assets/atlas.h"
#include "engine/assets/image.h"
#include "engine/core/error.h"
#include "engine/geometry/world.h"
#include "engine/image/atlas_map.h"
#include "engine/render/frustum.h"
#include "engine/render/world_view.h"
#include "interfaces/catalog/track.h"
#include "interfaces/geometry/model.h"
#include "interfaces/image/collection.h"

struct mh_image_decoder_provider;

/*
 * Backend-neutral mesh. Inspection builders fit to a unit radius; native
 * builders preserve original local/world positions. Quads use the 0-2
 * diagonal.
 */
struct mh_vertex {
	float	position[3];
	float	normal[3];
	float	uv[2];
	float	color[3];

	/*
	 * Preprojected effects can provide clip W for perspective UVs. Zero
	 * keeps ordinary matrix projection. Position remains normalized XYZ.
	 */
	float	projection_w;

	/* Normalized diffuse alpha; consumed by source-alpha sky blending. */
	float	opacity;
};

struct mh_draw {
	size_t	first, count, texture;

	/* MYO types 8/9 with flags & 0x60. */
	int	environment;

	/* Baked corner colors; no additional inspection light. */
	int	prelit;

	/* Textured MYO flag 2 uses the alpha-enabled batch. */
	int	alpha_test;

	/* Logical texture name for runtime track material flags. */
	char	texture_name[32];
};

struct mh_mesh_scenery;
struct mh_mesh_world;

/*
 * Original face boundaries, independent of lighting metadata. A quad keeps
 * all four corners for distant-fog dispatch and depth-bucket selection.
 */
struct mh_mesh_primitive {
	size_t		first, draw;

	/* Three or six expanded vertices. */
	unsigned int	count;

	/* Original MYO dispatch, retained for world faces too. */
	unsigned char	type, flags;
};

struct mh_mesh_face {
	size_t		first;

	/* Expanded triangle-list vertices: three or six. */
	unsigned int	count;

	/* Original first RGB, including textured faces. */
	unsigned char	color[3];
	float		plane[4];

	/* Original MYO material dispatch inputs. */
	unsigned char	type, flags;
};

#define MH_MESH_LODS 4

/*
 * Fixed geometry slices can be submitted independently without duplicating
 * vertices or textures. Indices refer to the owning mesh's complete arrays.
 */
struct mh_mesh_span {
	size_t	draw_first, draw_count;
	size_t	primitive_first, primitive_count;
};

/* Borrowed local scenery geometry and its current rigid world transform. */
struct mh_mesh_object {
	const struct mh_vertex		*vertices;
	const struct mh_mesh_face	*faces;
	size_t				vertex_first, vertex_count, face_count;
	struct mh_mesh_span		span;
	float				model[16];

	/* Current scenery animation RGB. */
	float				tint[3];
};

struct mh_mesh_lod {
	size_t	vertex_first, vertex_count;
	size_t	draw_first, draw_count;
	size_t	face_first, face_count;
	size_t	primitive_first, primitive_count;
	size_t	halo_first, halo_count;
};

struct mh_mesh {
	struct mh_vertex		*vertices;
	size_t				vertex_count;

	/*
	 * Optional owned raw shaded RGB, in byte units before Direct3D packing.
	 */
	float				*material_rgb;
	size_t				material_rgb_count;
	struct mh_draw			*draws;
	size_t				draw_count;

	/* Owned per-draw object visibility; NULL means all visible. */
	unsigned char			*draw_hidden;

	/* Owned per-face software visibility; NULL means all visible. */
	unsigned char			*primitive_hidden;

	/* Owned registration indices +1; zero untextured, NULL unregistered. */
	unsigned char			*draw_groups;

	/* Owned grid, model spheres and draw ranges, or NULL. */
	struct mh_mesh_world		*world;

	/*
	 * Optional owned indexed table, keyed by draw.texture, separate from
	 * RGBA.
	 */
	struct mh_indexed_image		*indexed_textures;
	size_t				indexed_texture_count;
	struct mh_image			*textures;
	size_t				texture_count;

	/*
	 * Owned preparation formats; later appended effect images are untagged.
	 */
	unsigned char			*texture_formats;
	size_t				texture_format_count;

	/* Zero until a map is supplied. */
	size_t				environment_texture;

	/* Borrowed shared pages, fixed until GPU destruction. */
	const struct mh_atlas		*atlas;

	/* Owned per-image page indices; (size_t)-1 keeps a standalone image. */
	size_t				*atlas_images;

	/* Owned normalized page UV pairs, independent of animated vertices. */
	float				*atlas_uv;

	/* Native single models retain material inputs. */
	struct mh_mesh_face		*faces;
	size_t				face_count;
	struct mh_mesh_primitive	*primitives;
	size_t				primitive_count;

	/* Owned local sources for native single models. */
	struct mh_geometry_halo		*halos;
	size_t				halo_count;

	/* Owned scenery poses/geometry; includes static scenery objects. */
	struct mh_mesh_scenery		*scenery;
	struct mh_mesh_lod		lods[MH_MESH_LODS];

	/* Zero count means an ordinary single mesh. */
	unsigned int			lod_count, lod;

	/* Native root MYO radius; unaffected by detail selection. */
	float				radius;

	/* Owned original pre-brightness RGB for static world vertices. */
	unsigned char			*world_colors;

	/* Software consumes these before material-specific saturation. */
	float				world_brightness[3];

	/* Scenery after this range keeps object lighting. */
	size_t				world_vertex_count;
};

/*
 * Translation-only assembly for static inspection. Models stay in their
 * original units until the complete assembly is fitted to one unit radius.
 */
struct mh_mesh_part {
	const struct mh_model	*model;
	double			offset[3];
};

struct mh_scene_instance {
	const struct mh_model		*model;
	const struct mh_scene_object	*pose;
};

/*
 * Cull complete retained scenery instances with the original root spheres.
 * Static world draws stay visible. NULL frustum restores all scenery draws.
 * No geometry/range changes or allocations; failed updates are atomic.
 */

struct mh_scene_transform {
	/* Index in the builder's original instances array. */
	size_t	scene_index;
	double	position[3], rotation[9];
};

enum mh_error_code	mh_mesh_build_parts(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_mesh_part *parts, size_t count,
	const struct mh_image_collection *collection, struct mh_mesh *mesh,
	struct mh_error *error_out);

/*
 * Requires validated decoded models. Owns all output data; model values and
 * the image collection need only remain alive until the build returns.
 * A missing texture is an error. Pass NULL collection for untextured models.
 */
enum mh_error_code	mh_mesh_build(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_model *model,
	const struct mh_image_collection *collection, struct mh_mesh *mesh,
	struct mh_error *error_out);

/*
 * Overview of world geometry pools, fitted as a whole. Shares one image
 * table across all cells; does not include separately placed objects.
 * Brightness is a finite, nonnegative RGB multiplier; output unchanged on
 * error.
 */
enum mh_error_code	mh_mesh_build_world(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world,
	const struct mh_image_collection *collection,
	const double brightness[3], struct mh_mesh *mesh,
	struct mh_error *error_out);

/*
 * Reapply a runtime brightness from original bytes, never from clamped RGB.
 * Does not change scenery colors, geometry, UVs or the source MYW.
 */
enum mh_error_code	mh_mesh_world_brightness(struct mh_mesh *mesh,
	const double brightness[3], struct mh_error *error_out);

/* PC 3.1 Direct3D/PowerVR world color stores and integer packing. */
enum mh_error_code	mh_mesh_material_world_brightness(struct mh_mesh *mesh,
	const double brightness[3], struct mh_error *error_out);

/* Retain raw colors separately from normalized upload vertices. */
enum mh_error_code	mh_mesh_prepare_material_rgb(struct mh_mesh *mesh,
	struct mh_error *error_out);

/*
 * Adds scenery before fitting to the world's bounds, retaining a private
 * copy of animated motion and local vertices. Repeated
 * model pointers share decoded image slots. Local normals/materials are
 * retained for native point lighting; scene RGB channels tint without scaling
 * geometry.
 */
enum mh_error_code	mh_mesh_build_world_scene(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world,
	const struct mh_image_collection *collection,
	const double brightness[3], const struct mh_scene_instance *instances,
	size_t count, struct mh_mesh *mesh, struct mh_error *error_out);

/*
 * Driving geometry: original units, owned output, no fit or origin shift.
 * World scenery keeps the same native animation support.
 */
enum mh_error_code	mh_mesh_build_native(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_model *model,
	const struct mh_image_collection *collection, struct mh_mesh *mesh,
	struct mh_error *error_out);

/* Software applies the original load-time type 4..7 color reduction. */
enum mh_error_code	mh_mesh_build_native_profile(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_model *model,
	const struct mh_image_collection *collection,
	enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out);

/*
 * Pack independent body variants into fixed owned arrays. All indices are
 * relocated once; switching lod never reallocates vertices, images or halos.
 * Output unchanged on failure, with no surviving dependency on the inputs.
 */
enum mh_error_code	mh_mesh_build_lods_profile(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_model *models, unsigned int count,
	const struct mh_image_collection *collection,
	enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out);
enum mh_error_code	mh_mesh_build_world_native_profile(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world,
	const struct mh_image_collection *collection,
	const double brightness[3], const struct mh_scene_instance *instances,
	size_t count, enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out);

/*
 * Selected slices, or the complete arrays for a single mesh. Validates bounds
 * before consumers use mutable lod metadata. Output unchanged on failure.
 */
enum mh_error_code	mh_mesh_active_lod(const struct mh_mesh *mesh,
	struct mh_mesh_lod *range, struct mh_error *error_out);
enum mh_error_code	mh_mesh_build_world_native(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world,
	const struct mh_image_collection *collection,
	const double brightness[3], const struct mh_scene_instance *instances,
	size_t count, struct mh_mesh *mesh, struct mh_error *error_out);

/*
 * World scene submission: span zero is the static world, followed by one
 * span per scenery instance in input scene order, independent of shared-model
 * storage order. Other mesh kinds return zero spans. Failed lookups preserve
 * output; returned indices remain valid through animation and culling.
 */
size_t	mh_mesh_scene_span_count(const struct mh_mesh *mesh);
int	mh_mesh_scene_span(const struct mh_mesh *mesh, size_t index,
	struct mh_mesh_span *span);

/*
 * Zero-based scene instance; native geometry only. Re-query after animation
 * or physics changes. Local vertices/materials are immutable and borrowed
 * until mesh destruction. Tint exposes the current motion RGB separately
 * from the rigid model transform. Invalid lookup leaves output unchanged.
 */
int	mh_mesh_scene_object(const struct mh_mesh *mesh, size_t index,
	struct mh_mesh_object *object);

/*
 * Register world cells followed by LWS instances, or all ordinary model
 * draws (including inactive LODs). The shared table and mesh are unchanged
 * on failure. Registration precedes GPU creation; it never uses visibility.
 */
enum mh_error_code	mh_mesh_register_textures(struct mh_mesh *mesh,
	const struct mh_atlas_map *remap, struct mh_texture_groups *groups,
	struct mh_error *error_out);

/*
 * Bind named diffuse draws to shared pages without changing source images or
 * animated vertices. Requires disjoint draw ranges and original byte-derived
 * UVs; output is atomic.
 * Rebinding is allowed before GPU creation and preserves standalone effects.
 */
enum mh_error_code	mh_mesh_bind_atlas(struct mh_mesh *mesh,
	const struct mh_atlas_map *remap, const struct mh_atlas *atlas,
	struct mh_error *error_out);

/*
 * Resolve a startup texture slot through optional shared atlas bindings.
 * NULL means invalid metadata. Returned pixels are immutable and borrowed.
 */
const struct mh_image	*mh_mesh_texture(const struct mh_mesh *mesh,
	size_t index);
void			 mh_mesh_free(struct mh_mesh *mesh);

/*
 * Add flag 2 to textures named by the track's ZeroTransparent list. Applies to
 * world, scenery and cars without modifying disk bytes.
 */
void	mh_mesh_track_materials(struct mh_mesh *mesh,
	const struct mh_track *track);

/*
 * Shade retained scenery at its last sampled/physics pose. World baked RGB
 * and geometry are preserved. Caller owns reusable light scratch.
 */
enum mh_error_code	mh_mesh_light_scene(struct mh_mesh *mesh,
	const struct mh_world *world, const double ambient[3],
	const double brightness[3], double intensity, const double eye[3],
	struct mh_prepared_light *scratch, size_t capacity,
	struct mh_error *error_out);

/* PC 3.1 Direct3D/PowerVR object-mode material composition and packing. */
enum mh_error_code	mh_mesh_material_light_scene(struct mh_mesh *mesh,
	const struct mh_world *world, const double ambient[3],
	const double brightness[3], double intensity, const double eye[3],
	struct mh_prepared_light *scratch, size_t capacity,
	struct mh_error *error_out);

/*
 * Updates retained scenery poses; no input assets or per-frame allocation.
 * Static world geometry, draws and images stay intact. On failure, vertices
 * remain unchanged. Seconds must be finite and in [0, 1e9].
 */
enum mh_error_code	mh_mesh_animate(struct mh_mesh *mesh, double seconds,
	struct mh_error *error_out);

/*
 * Scenery halos follow the same retained animation/physics pose as geometry.
 * Returned endpoints use mesh coordinates; RGB is the raw animated tint.
 * Failed indexed lookups preserve outputs.
 */
size_t	mh_mesh_scene_halo_count(const struct mh_mesh *mesh);

/*
 * Stable source-slot span for one original scenery instance (zero-based
 * LWS index). The source pool's storage order is independent of scene order.
 * Failed lookups preserve both outputs, including for an invalid instance.
 */
int	mh_mesh_scene_halo_span(const struct mh_mesh *mesh, size_t scene_index,
	size_t *first, size_t *count);
int	mh_mesh_scene_halo(const struct mh_mesh *mesh, size_t index,
	struct mh_geometry_halo *halo, float tint[3]);

/*
 * Parent sphere for the same indexed source. Original root translation and
 * MYO radius (recomputed from bounds for physical scenery), unaffected by
 * object rotation/scale or endpoint positions.
 * Inspection fitting applies to both outputs. Failure preserves outputs.
 */
int	mh_mesh_scene_halo_bounds(const struct mh_mesh *mesh, size_t index,
	float position[3], float *radius);

/* Match the selected renderer's sphere-test arithmetic. */
enum mh_error_code	mh_mesh_scene_cull_profile(
	enum mh_render_profile profile, struct mh_mesh *mesh,
	const struct mh_frustum *frustum, struct mh_error *error_out);
enum mh_error_code	mh_mesh_scene_cull(struct mh_mesh *mesh,
	const struct mh_frustum *frustum, struct mh_error *error_out);

/*
 * Select grid cells, then test each referenced world's model sphere.
 * The view is in the mesh's original world coordinates, even for a fitted
 * inspection mesh. NULL view restores only world draws. Input bytes need
 * not remain alive; no allocation or range/vertex changes during culling.
 */
enum mh_error_code	mh_mesh_world_cull(struct mh_mesh *mesh,
	const struct mh_world_view *view, struct mh_error *error_out);

/*
 * Native world, optionally with scenery, with increasing cell/model order,
 * prepared by mh_mesh_world_cull using a software view. Recompute visibility
 * for each camera before lens drawing; world geometry uses its identity model
 * transform. Other grid orders need an explicit producer list.
 */
int	mh_mesh_world_software_ready(const struct mh_mesh *mesh);

/*
 * Insertion point before this cell's geometry in an ordered world mesh.
 * Empty cells use the next model's first primitive, or the world's end.
 * This lets world halo markers precede their cell without splitting meshes.
 */
int	mh_mesh_world_cell_primitive(const struct mh_mesh *mesh, size_t cell,
	size_t *first);

/*
 * Sample RGB/animation, then replace selected scenery transforms with physics
 * poses, retaining the motion integrator's accumulated rotation drift.
 * Static bodies retain local geometry too. Unknown/duplicate indices and
 * invalid transforms fail atomically. No per-frame allocation.
 */
enum mh_error_code	mh_mesh_animate_objects(struct mh_mesh *mesh,
	double seconds, const struct mh_scene_transform *poses, size_t count,
	struct mh_error *error_out);

/*
 * Decode through the selected provider, apply the supplied precision and
 * append/replace the environment image and retain its format metadata. Keeps
 * mesh unchanged on failure. Ordinary diffuse textures and their indices stay
 * intact.
 */
enum mh_error_code	mh_mesh_set_environment(
	const struct mh_image_decoder_provider *decoder, struct mh_mesh *mesh,
	const unsigned char *data, size_t size, enum mh_texture_format format,
	struct mh_error *error_out);

/*
 * Load named draw textures from the supplied collection without changing
 * RGBA/UV. Replaces the whole indexed table only on success. Destroy renderers
 * borrowing this mesh before replacing textures. Archive/profile selection
 * stays explicit.
 */
enum mh_error_code	mh_mesh_load_indexed_textures(
	const struct mh_image_decoder_provider *decoder, struct mh_mesh *mesh,
	const struct mh_image_collection *collection,
	struct mh_error *error_out);

/*
 * Reload named diffuse images from the caller-selected collections:
 * ZeroTransparent names use the cutout collection, others the opaque
 * collection. Apply 3DAccBrightness followed by the supplied opaque
 * (P8/RGB565/RGB555/XRGB8888) or alpha (ARGB1555/ARGB4444) sampling precision.
 * Call before GPU upload. Idempotent, atomic on error; synthetic whites and
 * appended environment maps are preserved. Source dimensions must match
 * the geometry builder's texture table (as they do in all shipped archives).
 */
enum mh_error_code	mh_mesh_prepare_track_textures(
	const struct mh_image_decoder_provider *decoder, struct mh_mesh *mesh,
	const struct mh_image_collection *cutout_collection,
	const struct mh_image_collection *opaque_collection,
	const struct mh_track *track, enum mh_texture_format opaque_format,
	enum mh_texture_format alpha_format, struct mh_error *error_out);

/* Zero means untagged RGBA; atlas bindings override standalone precision. */
unsigned int	mh_mesh_texture_format(const struct mh_mesh *mesh,
	size_t texture_index);

#endif
