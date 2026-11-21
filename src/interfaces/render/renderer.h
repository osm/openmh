#ifndef MH_INTERFACES_RENDER_RENDERER_H
#define MH_INTERFACES_RENDER_RENDERER_H

/* Backend-independent scene and effect submission. */
#include "engine/core/error.h"
#include "engine/core/file.h"
#include "engine/render/fog.h"
#include "engine/render/halo.h"
#include "engine/render/hidden_weather.h"
#include "engine/render/mesh.h"
#include "engine/render/profile.h"
#include "engine/render/projection.h"
#include "engine/render/scene_face.h"
#include "engine/render/viewport.h"
#include "engine/ui/draw.h"
#include "engine/ui/retro.h"
#include "interfaces/render/effects.h"
#include "interfaces/render/glenz.h"
#include "interfaces/render/lighting.h"
#include "interfaces/window/window.h"

struct mh_image;
struct mh_renderer;
struct mh_render_backend;

enum mh_renderer_flags {
	MH_RENDERER_DIAGNOSTICS = 1
};

enum mh_texture_filter {
	MH_TEXTURE_FILTER_ORIGINAL,
	MH_TEXTURE_FILTER_NEAREST,
	MH_TEXTURE_FILTER_BILINEAR,
	MH_TEXTURE_FILTER_TRILINEAR
};

/* Original software texture correction policy, independent of the GPU API. */
enum mh_texture_mapping {
	MH_TEXTURE_AFFINE,
	MH_TEXTURE_SELECTIVE,
	MH_TEXTURE_PERSPECTIVE
};

/* Depth-cue gradient table capacity. */
#define MH_DEPTH_CUE_ROWS 1280

struct mh_depth_cue_gradient {
	float		top;
	unsigned int	height;
	float		above[3], below[3];

	/*
	 * Retain unused rows across panorama reloads; initialize once to zero.
	 */
	float		rows[MH_DEPTH_CUE_ROWS][3];
};

/* Original software CueColour (unpacked RGB) and CueStart (far fraction). */
struct mh_depth_cue {
	float					color[3];
	float					start;
	const struct mh_depth_cue_gradient	*gradient;
};

struct mh_panorama {
	const struct mh_image	*image;
	float			horizon;
	unsigned char		above[3], below[3], clear[3];
	int			height_cue;
};

/* Original world producer or ordinary model root and attached children. */
enum mh_renderer_model_role {
	MH_RENDER_MODEL_ROOT,
	MH_RENDER_MODEL_CHILD,
	MH_RENDER_MODEL_WORLD
};

struct mh_renderer_scene_part {
	size_t				mesh;
	enum mh_renderer_model_role	model_role;

	/* Nonzero submits the mesh's currently selected detail. */
	int				active_lod;

	/* Used only when active_lod is zero. */
	struct mh_mesh_span		span;
};

struct mh_renderer_tron_face {
	enum mh_scene_pass	pass;
	unsigned int		count;
	int			smooth;
	struct mh_scene_vertex	corners[4];
	size_t			model;

	/* Existing scene texture, used only for cutouts. */
	size_t			texture;
};

#define MH_FRAME_QUEUE_MIN 1
#define MH_FRAME_QUEUE_MAX 3

/*
 * GPU scene textures: Original preserves material-specific Glide sampling;
 * other choices override ordinary and Glenz materials. Trilinear samples up
 * to three levels on ordinary textures; environment/Glenz maps stay at base
 * level. UI, sky and effect sprites retain their own sampling. Software
 * returns UNSUPPORTED. Changes apply to subsequent frames, without reload.
 * Failure leaves the current choice and completed picture unchanged.
 */
enum mh_error_code	mh_renderer_set_texture_filter(
	struct mh_renderer *renderer, enum mh_texture_filter filter,
	struct mh_error *error_out);

/* NULL disables Glenz for this mesh. Software returns UNSUPPORTED. */
enum mh_error_code	mh_renderer_mesh_glenz(struct mh_renderer *renderer,
	size_t index, const struct mh_renderer_glenz *glenz,
	struct mh_error *error_out);

/*
 * Software defaults to selective correction with 32-pixel blocks. Block size
 * is 4, 8, 16 or 32 pixels (original menu: Good=16, Normal=32). Applies to
 * subsequent frames; failures preserve settings and the completed picture.
 * Backends without block sampling return MH_ERROR_UNSUPPORTED.
 */
enum mh_error_code	mh_renderer_set_texture_mapping(
	struct mh_renderer *renderer, enum mh_texture_mapping mapping,
	unsigned int block_pixels, struct mh_error *error_out);

/*
 * Software distance cue, applied per source face in lens frames. RGB must be
 * clamped to 0..255; fractional channels are retained. start is in [0,1). NULL
 * disables it. Copies settings; errors preserve the previous settings and
 * picture.
 *
 * An optional gradient selects sky row colors from average projected Y,
 * excluding near corners from the sum (not the divisor). All gradient channels
 * are in 0..255; top is a bounded screen Y, height is 1..MH_DEPTH_CUE_ROWS.
 * Exact bottom equality reads rows[height], or below at the full table height,
 * preserving the original boundary rule. The whole gradient is copied; caller
 * storage may be released afterward. The caller supplies the prepared panorama
 * rows and its current screen top.
 *
 * Glide rejects this operation; its iterated fog uses mh_renderer_set_fog.
 * Matrix inspection frames reject an enabled cue because they lack a lens.
 */
enum mh_error_code	mh_renderer_set_depth_cue(struct mh_renderer *renderer,
	const struct mh_depth_cue *cue, struct mh_error *error_out);

/*
 * Original software panorama, prepared from top-down RGBA8 (alpha ignored).
 * Copies pixels/settings; the caller may release the source after success.
 * Image axes are 1..4096, horizon is 0..1. Loading an image resets yaw/phase
 * and replaces the populated fog rows, retaining unused rows and the last top.
 * NULL image selects SkyImage Off: clear color, retained placement/row history.
 * NULL settings removes the background and restores inspection-frame clearing.
 *
 * height_cue explicitly selects panorama fog rows when an image is present;
 * normal original race setup disables it. It requires an image and cannot be
 * combined with a caller-supplied depth-cue gradient. Cue amount/enabling still
 * comes from set_depth_cue. Without height_cue, cue settings are unchanged.
 *
 * Direct lens frames place sky before geometry and use its current fog top.
 * Motion blur prepares faces with the preceding top, then draws history sky.
 * Scene drawing with configured panoramas requires lens frames. UI-only frames
 * replace the image without drawing or advancing the retained panorama.
 *
 * Failures preserve settings, state and the last picture. Glide uses sky meshes
 * and rejects this operation.
 */
enum mh_error_code	mh_renderer_set_panorama(struct mh_renderer *renderer,
	const struct mh_panorama *panorama, struct mh_error *error_out);

/*
 * Diagnostic from the last boolean operation on this renderer. A successful
 * boolean operation clears it; enum/status operations use their error output.
 * Message is owned by the renderer until its next boolean operation or destroy.
 */
enum mh_error_code	mh_renderer_error(const struct mh_renderer *renderer,
	struct mh_error *error_out);

/*
 * Open the explicitly selected backend. A NULL window requests offscreen
 * rendering. Meshes and window are borrowed until destroy. Output is unchanged
 * on failure; diagnostics follow the selected backend's borrowed lifetime.
 */
enum mh_error_code	mh_renderer_open(
	const struct mh_render_backend *backend, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out);

/* Metadata queries require a live renderer. */
enum mh_render_profile	mh_renderer_profile(const struct mh_renderer *renderer);
const char	       *mh_renderer_name(const struct mh_renderer *renderer);

/*
 * Copy a complete submission order, preserving mesh/transform indices. Each
 * mesh must occur once as active_lod, or as nonoverlapping fixed spans covering
 * all its draws/primitives. Fixed spans require a mesh without LODs. NULL/zero
 * restores mesh array order. Failure preserves the previous order.
 *
 * Children follow their root and precede the next root or world part. World
 * parts do not parent children. Roles select software depth bands, not
 * transform inheritance or visibility. Callers supply final transforms and
 * visibility for every mesh. Zero-initialized parts are roots. GPU rendering
 * retains its existing depth rules.
 *
 * Software accepts whole meshes and fixed spans of its supported geometry,
 * including active LOD chains. The world role selects the original mode-1
 * bands. A native world without scenery is accepted after mh_mesh_world_cull
 * with a software view, provided its cell order follows model storage. Its
 * faces always use world bands, including in default mesh order. Recompute
 * world visibility for each lens camera and use an identity world model
 * transform.
 *
 * Material/effect passes still apply their own list ordering afterward.
 */
int	mh_renderer_scene_order(struct mh_renderer *renderer,
	const struct mh_renderer_scene_part *parts, size_t count);

int	mh_renderer_update_mesh(struct mh_renderer *renderer, size_t index);

/*
 * Toggle a mesh without reallocating its renderer resources or changing
 * indices.
 */
int	mh_renderer_mesh_visible(struct mh_renderer *renderer, size_t index,
	int visible);

/* Supply CPU-lit opaque vertices; still receives textures/reflections/fog. */
int	mh_renderer_mesh_lit(struct mh_renderer *renderer, size_t index,
	const struct mh_vertex *vertices);

/*
 * Configure original-profile model lighting, copied with its selected lights.
 * Software applies it in lens frames using that frame's model/view transforms;
 * material RGB stays unnormalized until fog/raster conversion. Supports
 * standalone type-0..3 and type-8/9 models, including LOD chains. Unlit models
 * require configuration before drawing. NULL clears it. Invalid input preserves
 * settings and the last picture.
 *
 * Glide retains its CPU-lit vertex path and returns MH_ERROR_UNSUPPORTED. This
 * does not select world lights or infer object tint; those inputs belong to the
 * scene's resource/game state.
 */
enum mh_error_code	mh_renderer_model_lighting(struct mh_renderer *renderer,
	size_t index, const struct mh_model_lighting *lighting,
	struct mh_error *error_out);

/*
 * Select world lights during each frame. Software uses lens frames; OpenMH uses
 * matrix frames and GPU vertex lighting with the strongest 64 contributing
 * lights per object. OpenMH uses point lights/ambient/brightness/mode; software
 * directional and flat-detail controls do not apply to it.
 *
 * Copy settings and the parsed world view; decoded arrays stay borrowed and
 * immutable until clearing or destruction. Ordinary models select at the root
 * mesh's submitted origin and prepare at their own transform. A body's wheels
 * therefore share its selected cell. Packed scenery uses root == index and
 * OBJECT mode; each instance selects at its current pose. Packed scenery
 * requires an identity owning-mesh transform. No explicit light list is
 * allowed.
 *
 * Both NULL clears. Successful updates replace direct model lighting; direct
 * model-lighting updates likewise clear this source. Invalid updates preserve
 * settings and the last picture. Repeated settings updates reuse light scratch.
 * Glide retains CPU-lit vertices and returns UNSUPPORTED. Submission roles
 * remain separate.
 */
enum mh_error_code	mh_renderer_world_lighting(struct mh_renderer *renderer,
	size_t index, size_t root, const struct mh_world *world,
	const struct mh_model_lighting *lighting, struct mh_error *error_out);

/*
 * Copy the three original software environment slots (flags 0x20/0x40/0x60).
 * Each map is a 256x256 indexed image with its raw palette. NULL clears all
 * slots. Invalid updates preserve the maps and last picture. Required before
 * drawing reflective models; the car fallback can draw without these maps.
 * Glide keeps its mesh-owned environment image and returns UNSUPPORTED.
 */
enum mh_error_code	mh_renderer_set_environment(
	struct mh_renderer *renderer,
	const struct mh_indexed_image *const maps[3],
	struct mh_error *error_out);

/*
 * Supply vertex_count shaded vertices and select the additive, untextured
 * pass. NULL restores original vertices/opaque materials on state change.
 */
int	mh_renderer_mesh_unavailable(struct mh_renderer *renderer, size_t index,
	const struct mh_vertex *vertices);

/* Supply an untextured, prelit destination multiplier; no depth writes. */
int	mh_renderer_mesh_skids(struct mh_renderer *renderer, size_t index);

/*
 * Glide: variable per-vertex destination multiplier, after skids and before
 * particles. Software: world-space type-1 quads with flag zero, prelit draws,
 * raw world_colors all 10 and live world brightness. Software uses original
 * shadow depth bands, ordinary fog and near clipping in lens frames; no LOD,
 * scenery or model transform. Submit each car's span before its body root.
 */
int	mh_renderer_mesh_shadows(struct mh_renderer *renderer, size_t index);

/*
 * Textured additive particles, no depth writes. Mesh positions are already
 * clip-normalized; this pass uses an identity transform.
 */
int	mh_renderer_mesh_particles(struct mh_renderer *renderer, size_t index);

/*
 * Clip-normalized original sky before geometry. Glide does not write depth;
 * Direct3D lens frames write the supplied background depth with an ALWAYS test.
 * Panorama U repeats; V clamps. A sky draw using its installed environment
 * texture repeats both axes. Halos are additive and depth-tested after
 * geometry, without fog or alpha cutout.
 */
int	mh_renderer_mesh_sky(struct mh_renderer *renderer, size_t index);
int	mh_renderer_mesh_halos(struct mh_renderer *renderer, size_t index);

/*
 * Queue one depth pixel per active source after a scene frame. Read before
 * the next lamp update to retain the original one-frame visibility history.
 * A browser backend may retain the last completed history while its GPU is
 * busy, so visibility downloads cannot block input/audio on the event loop.
 * Depth is forward [0,1]; invisible/offscreen queries leave history alone.
 */
int	mh_renderer_depth_queue(struct mh_renderer *renderer,
	const struct mh_depth_probe *probes, size_t count);
int	mh_renderer_depth_read(struct mh_renderer *renderer,
	unsigned char *visible, size_t count);

/*
 * Enable original hardware distance fog on opaque meshes. NULL disables it
 * (the default for inspection tools). Effects and overlays bypass fog.
 */
int	mh_renderer_set_fog(struct mh_renderer *renderer,
	const struct mh_fog *fog);

/*
 * Hardware MotionBlur: sky over a retained complete frame (two swaps by
 * default; configurable through set_blur_history on supported backends).
 * Glide uses constant alpha 128/255; Direct3D lens frames use vertex opacity
 * (normally 127/255, zero for black overhead sky). World/UI draw normally;
 * TRON/UI-only frames clear.
 * Off by default. Enable before the first frame for original black startup.
 * First enable allocates a second color target if history was not configured;
 * failure leaves state alone. Allocated buffers track frames while disabled.
 */
int	mh_renderer_set_motion_blur(struct mh_renderer *renderer, int enabled);

/*
 * Choose two or three retained color targets, independently of frame queue.
 * Call before the first frame. Changing the count resets history to black;
 * repeating the current choice preserves history. Allocation failure leaves
 * the old history intact. Buffers track frames even while blur is disabled.
 * Optional; unsupported backends report UNSUPPORTED.
 */
enum mh_error_code	mh_renderer_set_blur_history(
	struct mh_renderer *renderer, unsigned int frames,
	struct mh_error *error_out);

/*
 * Supplied frame parameters apply to world/effects only. Sky and UI bypass
 * distortion; NULL restores ordinary projection. Does not advance phase.
 */
int	mh_renderer_set_water(struct mh_renderer *renderer,
	const struct mh_water_frame *frame);

/* models: count column-major rigid transforms, or NULL for identities. */
int	mh_renderer_scene_frame(struct mh_renderer *renderer,
	const float matrix[16], const float view[16], const float *models,
	int reflections);

/*
 * The optional overlay atlas is copied once and owned by the renderer.
 * UI frame data is consumed immediately, after the 3D pass and before blit.
 * Word-text commands borrow decoded immutable fonts until submission returns;
 * they preserve original software pixel writes and are rejected by Glide.
 */
int	mh_renderer_ui_upload(struct mh_renderer *renderer,
	const struct mh_ui_atlas *atlas);
int	mh_renderer_scene_frame_ui(struct mh_renderer *renderer,
	const float matrix[16], const float view[16], const float *models,
	int reflections, const struct mh_ui_list *ui);

/*
 * Original r3tr0 post-HUD overlay shares the scene depth buffer. NULL retro
 * is the ordinary path; input is consumed immediately and never retained.
 */
int	mh_renderer_scene_frame_retro(struct mh_renderer *renderer,
	const float matrix[16], const float view[16], const float *models,
	int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro);

/*
 * Submit ordinary meshes using explicit original lens caches. View/models are
 * column-major affine transforms with the same negative-forward view Z as the
 * matrix calls. The backend converts to its original camera convention. Inputs
 * are consumed immediately. Providers without original lens submission or
 * support for the requested effects return MH_ERROR_UNSUPPORTED.
 *
 * Software sorts whole source faces by original root/child depth buckets, using
 * scene_order roles or independent roots by default. World geometry follows the
 * software culling contract above. Matrix inspection keeps order without
 * depth-band sorting. Invalid/unsupported input preserves the last picture and
 * raster state.
 *
 * Optional names precede their body's halos/faces; halos retain their source
 * insertion positions. Skids and particles follow world/model producers.
 * Effects use their original depth bands; skid visibility feedback is published
 * only on frame success. See render/effects.h.
 */
enum mh_error_code	mh_renderer_scene_frame_lens(
	struct mh_renderer *renderer, const struct mh_projection *projection,
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
	const struct mh_renderer_effects *effects, struct mh_error *error_out);

/*
 * Projected faces in original submission order. Replaces the world pass,
 * clears black and preserves the ordinary UI pass. No input is retained.
 * Caller supplies projection/list ordering; this is not a race-mode toggle.
 * Textured corners need reciprocal W >= 1e-20 for finite projection.
 */
int	mh_renderer_tron_frame_ui(struct mh_renderer *renderer,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui);

/*
 * Build the hidden renderer's seven passes from current scene
 * geometry/colors and active LODs. Suppresses ordinary effect meshes.
 * View/models use the ordinary renderer's column-major rigid transforms.
 */
int	mh_renderer_tron_scene_ui(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui);

/* Menu-only frame on black, with the same supplied atlas. */
int	mh_renderer_ui_frame(struct mh_renderer *renderer,
	const struct mh_ui_list *ui);
void	mh_renderer_destroy(struct mh_renderer *renderer);

/*
 * Refresh the existing vertex data; mesh size/draws/images must not change.
 */
int	mh_renderer_update_vertices(struct mh_renderer *renderer);
int	mh_renderer_frame(struct mh_renderer *renderer, const float matrix[16],
	const float view[16], int reflections);
int	mh_renderer_capture(struct mh_renderer *renderer, const char *path);

/*
 * Owned top-down RGBA8, without file-format quantization. Caller frees rgba.
 * Failure preserves output. The renderer must have a completed frame.
 */
enum mh_error_code	mh_renderer_read_pixels(struct mh_renderer *renderer,
	struct mh_image *output, struct mh_error *error_out);

/*
 * Read the presented color target, quantize RGBA8 to RGB555 and encode
 * the original hardware TGA. Caller frees output->data; atomic on failure.
 */
int	mh_renderer_tga_capture(struct mh_renderer *renderer,
	struct mh_file *output);

/*
 * Limit frames pending for window presentation, not physical swapchain images.
 * May wait for existing GPU work. Optional: headless/CPU backends can report
 * UNSUPPORTED. Does not change vsync, simulation timing or pixel precision.
 */
enum mh_error_code	mh_renderer_set_frame_queue(
	struct mh_renderer *renderer, unsigned int frames,
	struct mh_error *error_out);

/*
 * Window presentation only; enabled is 0 or 1. Off requests rendering without
 * refresh throttling (immediate or mailbox presentation). Display policy may
 * still impose a limit. Absent support returns UNSUPPORTED.
 */
enum mh_error_code	mh_renderer_set_vsync(struct mh_renderer *renderer,
	int enabled, struct mh_error *error_out);

#endif
