add_library(mh_render_common STATIC
	src/engine/render/profile.c
	src/engine/render/matrix.c
	src/engine/render/material_classify.c
	src/engine/render/material_color.c
	src/engine/render/mipmap.c
	src/engine/render/halo.c
	src/engine/render/sky.c
	src/engine/render/brightness.c
	src/engine/render/fog.c
	src/engine/render/frustum.c
	src/engine/render/glenz.c
	src/engine/render/hidden_weather.c
	src/engine/render/lighting.c
	src/engine/render/lod.c
	src/engine/render/name_label.c
	src/engine/render/name_mesh.c
	src/engine/render/near_clip.c
	src/engine/render/object_light.c
	src/engine/render/pixel_snap.c
	src/engine/render/projection.c
	src/engine/render/retro.c
	src/engine/render/skid_face.c
	src/engine/render/scene_face.c
	src/engine/render/shadow.c
	src/engine/render/submission.c
	src/engine/render/pipeline.c
	src/engine/render/tron.c
	src/engine/render/tron_draw.c
	src/engine/render/unavailable_mesh.c
	src/engine/render/world_view.c)
target_include_directories(mh_render_common PUBLIC src)
target_link_libraries(mh_render_common PUBLIC mh_core mh_image mh_render_build)
mh_c89(mh_render_common)

# Keep single-precision operations separate on FMA-capable hosts.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	set_source_files_properties(
		src/engine/render/material_color.c
		src/engine/render/frustum.c
		src/engine/render/glenz.c
		src/engine/render/world_view.c
		PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

# Portable visual profiles share the rendering library and its C89 contract.
target_sources(mh_render_common PRIVATE
	src/engine/render/texture_policy.c
	src/engine/render/material_policy.c
	src/engine/render/scene_effect_policy.c
	src/engine/render/material_pipeline.c
	src/engine/render/face_fogged.c
	src/engine/render/face_vertex_color.c)

if("software" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING OR MH_BUILD_TOOLS)
	target_sources(mh_render_common PRIVATE
		src/engine/render/software_shadow.c
		src/engine/render/software_skid.c
		src/engine/render/software_skid_history.c)
endif()

if("glide" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING OR MH_BUILD_TOOLS)
	target_sources(mh_render_common PRIVATE
		src/engine/render/glide_texture_policy.c)
endif()

if("direct3d" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING OR MH_BUILD_TOOLS)
	target_sources(mh_render_common PRIVATE
		src/engine/render/direct3d_texture_policy.c
		src/engine/render/direct3d_effect_policy.c
		src/engine/render/direct3d_draw.c
		src/engine/render/direct3d_pipeline.c)
endif()

if("powervr" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING OR MH_BUILD_TOOLS)
	target_sources(mh_render_common PRIVATE
		src/engine/render/powervr_texture_policy.c
		src/engine/render/powervr_effect_policy.c
		src/engine/render/powervr_draw.c
		src/engine/render/powervr_pipeline.c
		src/engine/render/powervr_face.c
		src/engine/render/powervr_material.c
		src/engine/render/powervr_order.c
		src/engine/render/powervr_environment.c
		src/engine/render/powervr_halo.c
		src/engine/render/powervr_effect.c
		src/engine/render/powervr_lamp_atlas.c
		src/engine/render/powervr_texture.c
		src/engine/render/powervr_shadow.c)
endif()

# Preserve the original single-precision operations on FMA-capable hosts.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	set_source_files_properties(
		src/engine/render/powervr_effect.c
		src/engine/render/software_shadow.c
		src/engine/render/software_skid.c
		src/engine/render/software_skid_history.c
		PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

add_library(mh_renderer STATIC src/engine/render/renderer.c)
target_include_directories(mh_renderer PUBLIC src)
target_link_libraries(mh_renderer PUBLIC mh_error)
mh_c89(mh_renderer)

add_library(mh_canvas STATIC src/engine/render/canvas.c)
target_include_directories(mh_canvas PUBLIC src)
target_link_libraries(mh_canvas PUBLIC mh_error)
mh_c89(mh_canvas)

add_library(mh_image_capture STATIC src/engine/render/capture.c)
target_link_libraries(mh_image_capture PUBLIC mh_error)
mh_c89(mh_image_capture)

add_library(mh_software_raster STATIC EXCLUDE_FROM_ALL
	src/backends/render/software_blur.c
	src/backends/render/software_face.c
	src/backends/render/software_fixed.c
	src/backends/render/software_fog.c
	src/backends/render/software_halo_geometry.c
	src/backends/render/software_halo_visibility.c
	src/backends/render/software_light_prepare.c
	src/backends/render/software_lighting.c
	src/backends/render/software_name_geometry.c
	src/backends/render/software_near_clip.c
	src/backends/render/software_order.c
	src/backends/render/software_palette.c
	src/backends/render/software_perspective.c
	src/backends/render/software_projection.c
	src/backends/render/software_raster.c
	src/backends/render/software_reflection.c
	src/backends/render/software_skid_face.c
	src/backends/render/software_skid_light.c
	src/backends/render/software_sky.c
	src/backends/render/software_sky_image.c)
target_include_directories(mh_software_raster PUBLIC src)
target_link_libraries(mh_software_raster PUBLIC mh_core)

if(NOT MSVC)
	target_link_libraries(mh_software_raster PRIVATE m)
endif()
mh_c89(mh_software_raster)

# Preserve the original x87 operations and explicit binary32 store boundaries.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(mh_software_raster PRIVATE -ffp-contract=off)
endif()

add_library(mh_render_software STATIC EXCLUDE_FROM_ALL
	src/backends/render/software_environment.c
	src/backends/render/software_frame.c
	src/backends/render/software_geometry.c
	src/backends/render/software_halos.c
	src/backends/render/software_light_select.c
	src/backends/render/software_mesh.c
	src/backends/render/software_model_light.c
	src/backends/render/software_names.c
	src/backends/render/software_panorama.c
	src/backends/render/software_particles.c
	src/backends/render/software_presentation_canvas.c
	src/backends/render/software_renderer.c
	src/backends/render/software_scene.c
	src/backends/render/software_skids.c
	src/backends/render/software_text.c
	src/backends/render/software_ui.c
	src/backends/render/software_world_light.c)
target_include_directories(mh_render_software PUBLIC src)
target_link_libraries(mh_render_software PUBLIC
	mh_renderer
	mh_canvas
	mh_software_raster
	mh_assets
	mh_render_common)
mh_c89(mh_render_software)

# Embed these objects in mh_assets alongside the readers used by construction.
# Keeping one archive resolves reader/builder calls without a static-library cycle.
add_library(mh_mesh OBJECT
	src/engine/render/mesh_atlas.c
	src/engine/render/mesh_environment.c
	src/engine/render/mesh_halos.c
	src/engine/render/mesh_indexed.c
	src/engine/render/mesh_lighting.c
	src/engine/render/mesh_lod.c
	src/engine/render/mesh.c
	src/engine/render/mesh_model.c
	src/engine/render/mesh_motion.c
	src/engine/render/mesh_textures.c
	src/engine/render/mesh_visibility.c
	src/engine/render/mesh_world.c)
if("openmh" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING OR MH_BUILD_TOOLS)
	target_sources(mh_mesh PRIVATE src/engine/render/openmh_light_buffer.c)
endif()
target_include_directories(mh_mesh PUBLIC src)
target_link_libraries(mh_mesh
	PUBLIC
		mh_core
		mh_render_common
	PRIVATE
		mh_image_decoding)
mh_c89(mh_mesh)

# Keep single-precision operations separate on FMA-capable hosts.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	set_source_files_properties(
		src/engine/render/mesh_visibility.c
		PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

add_library(mh_atmosphere_mesh STATIC
	src/engine/render/atmosphere_attached_lamps.c
	src/engine/render/atmosphere_lamps.c
	src/engine/render/atmosphere_mesh.c
	src/engine/render/atmosphere_sky.c
	src/engine/render/atmosphere_world_lamps.c)
target_include_directories(mh_atmosphere_mesh PUBLIC src)
target_link_libraries(mh_atmosphere_mesh
	PUBLIC
		mh_assets
	PRIVATE
		mh_image_decoding)
mh_c89(mh_atmosphere_mesh)

# Geometry derived from live simulation state, above the asset mesh layer.
add_library(mh_effect_mesh STATIC
	src/engine/render/effect_particles.c
	src/engine/render/effect_shadows.c
	src/engine/render/effect_skids.c)
if("software" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING OR MH_BUILD_TOOLS)
	target_sources(mh_effect_mesh PRIVATE src/engine/render/software_shadow_mesh.c)
endif()
target_include_directories(mh_effect_mesh PUBLIC src)
target_link_libraries(mh_effect_mesh PUBLIC mh_sim)
mh_c89(mh_effect_mesh)

# Portable model transforms, detail selection and per-frame lighting inputs.
add_library(mh_scene_models STATIC
	src/engine/render/scene_finish.c
	src/engine/render/scene_matrices.c
	src/engine/render/scene_detail.c
	src/engine/render/scene_visibility.c)
if("software" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING OR MH_BUILD_TOOLS)
	target_sources(mh_scene_models PRIVATE src/engine/render/software_scene_lighting.c)
endif()
target_include_directories(mh_scene_models PUBLIC src)
target_link_libraries(mh_scene_models PUBLIC mh_sim mh_mesh mh_assets)
mh_c89(mh_scene_models)

add_library(mh_race_scene STATIC
	src/engine/render/scene_interpolation.c
	src/engine/render/scene_finish_load.c
	src/engine/render/scene_finish_light.c
	src/engine/render/scene_cameras.c
	src/engine/render/scene_glenz.c
	src/engine/render/scene_lighting.c
	src/engine/render/scene_objects.c
	src/engine/render/scene_effect_order.c
	src/engine/render/scene_gpu_order.c
	src/engine/render/scene_halos.c
	src/engine/render/scene_car_load.c
	src/engine/render/scene_effect_load.c
	src/engine/render/scene_lamp_load.c
	src/engine/render/scene_track_load.c
	src/engine/render/scene_textures.c
	src/engine/render/scene_load.c
	src/engine/render/scene_names.c
	src/engine/render/scene_particle_load.c
	src/engine/render/scene_particles.c
	src/engine/render/scene_skids.c)
if("software" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING OR MH_BUILD_TOOLS)
	target_sources(mh_race_scene PRIVATE
		src/engine/render/software_scene_order.c
		src/engine/render/software_scene_setup.c
		src/engine/render/software_scene_frame.c
		src/engine/render/software_scene_assets.c
		src/engine/render/software_scene_particle_assets.c
		src/engine/render/software_scene_halo_assets.c
		src/engine/render/software_scene_halos.c
		src/engine/render/software_scene_names.c)
endif()

if("openmh" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING)
	target_sources(mh_race_scene PRIVATE src/engine/render/openmh_scene_lighting.c)
endif()

if("powervr" IN_LIST MH_RENDER_PROFILES)
	target_sources(mh_race_scene PRIVATE src/engine/render/scene_lamp_atlas.c)
endif()
target_include_directories(mh_race_scene PUBLIC src)
target_link_libraries(mh_race_scene
	PUBLIC
		mh_retro_loading
		mh_material_loading
		mh_navigation_loading
		mh_collision_loading
		mh_asset_loaders
		mh_effect_mesh
		mh_scene_models
		mh_settings
		mh_atmosphere_mesh
	PRIVATE
		mh_image_loading
		mh_font_loading
		mh_renderer)
mh_c89(mh_race_scene)

# Portable scene coordination selects profiles; common math stays independent.
add_library(mh_scene_materials STATIC
	src/engine/render/scene_material_face.c
	src/engine/render/scene_material_policy.c)
target_link_libraries(mh_scene_materials PUBLIC mh_render_common)
mh_c89(mh_scene_materials)

add_library(mh_scene_tron STATIC src/engine/render/scene_tron.c)
target_link_libraries(mh_scene_tron PUBLIC mh_render_common)
mh_c89(mh_scene_tron)
