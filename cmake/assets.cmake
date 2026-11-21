add_library(mh_collision_data STATIC src/engine/collision/data.c)
target_include_directories(mh_collision_data PUBLIC src)
mh_c89(mh_collision_data)

add_library(mh_collision_decoding STATIC src/engine/collision/decode.c)
target_link_libraries(mh_collision_decoding PUBLIC mh_collision_data mh_error)
mh_c89(mh_collision_decoding)

add_library(mh_collision_loading STATIC src/engine/collision/load.c)
target_link_libraries(mh_collision_loading PUBLIC
	mh_collision_decoding
	mh_resource_source)
mh_c89(mh_collision_loading)

add_library(mh_model_data STATIC src/engine/geometry/model.c)
target_include_directories(mh_model_data PUBLIC src)
mh_c89(mh_model_data)

add_library(mh_world_data STATIC src/engine/geometry/world.c)
target_include_directories(mh_world_data PUBLIC src)
target_link_libraries(mh_world_data PUBLIC mh_core)
mh_c89(mh_world_data)

add_library(mh_geometry_decoding STATIC src/engine/geometry/decode.c)
target_link_libraries(mh_geometry_decoding PUBLIC
	mh_model_data
	mh_world_data
	mh_error)
mh_c89(mh_geometry_decoding)

add_library(mh_geometry_loading STATIC src/engine/geometry/load.c)
target_link_libraries(mh_geometry_loading PUBLIC
	mh_geometry_decoding
	mh_resource_source)
mh_c89(mh_geometry_loading)

add_library(mh_wireframe STATIC
	src/engine/geometry/wireframe.c
	src/engine/geometry/wireframe_load.c)
target_include_directories(mh_wireframe PUBLIC src)
target_link_libraries(mh_wireframe PUBLIC mh_error)
mh_c89(mh_wireframe)

add_library(mh_animation STATIC
	src/engine/animation/scene.c
	src/engine/animation/load.c)
target_link_libraries(mh_animation PUBLIC mh_core)
mh_c89(mh_animation)

add_library(mh_navigation_data STATIC
	src/engine/navigation/route.c
	src/engine/navigation/camera_motion.c
	src/engine/navigation/ai_path.c)
target_link_libraries(mh_navigation_data PUBLIC mh_animation)
mh_c89(mh_navigation_data)

add_library(mh_navigation_loading STATIC src/engine/navigation/load.c)
target_link_libraries(mh_navigation_loading PUBLIC mh_navigation_data mh_error)
mh_c89(mh_navigation_loading)

add_library(mh_catalog_data STATIC
	src/engine/catalog/profile.c
	src/engine/catalog/car.c
	src/engine/catalog/track.c
	src/engine/catalog/halo_paths.c
	src/engine/catalog/catalog.c)
target_link_libraries(mh_catalog_data PUBLIC mh_error mh_horn)
mh_c89(mh_catalog_data)

add_library(mh_catalog_loading STATIC src/engine/catalog/load.c)
target_link_libraries(mh_catalog_loading PUBLIC
	mh_catalog_data
	mh_resource_source)
mh_c89(mh_catalog_loading)

add_library(mh_profile_loading STATIC
	src/engine/catalog/profile_source.c
	src/engine/catalog/profile_load.c)
target_link_libraries(mh_profile_loading PUBLIC
	mh_catalog_data
	mh_resource_source)
mh_c89(mh_profile_loading)

add_library(mh_material_loading STATIC src/engine/material/load.c)
target_link_libraries(mh_material_loading PUBLIC mh_error)
mh_c89(mh_material_loading)

add_library(mh_icon STATIC
	src/engine/assets/ico.c
	src/engine/assets/icon.c
	src/engine/assets/executable_icon.c)
target_link_libraries(mh_icon PUBLIC mh_resource_source PRIVATE mh_core)
mh_c89(mh_icon)

add_library(mh_content STATIC
	src/engine/assets/content_probe.c
	src/engine/assets/packages.c
	src/engine/assets/content_edition.c)
target_link_libraries(mh_content
	PUBLIC
		mh_vfs
		mh_resource_digest
	PRIVATE
		mh_executable_identity)
mh_c89(mh_content)

add_library(mh_image STATIC src/engine/assets/image.c)
target_include_directories(mh_image PUBLIC src)
target_link_libraries(mh_image PUBLIC mh_error)
mh_c89(mh_image)

add_library(mh_asset_encoding STATIC
	src/engine/assets/key.c
	src/engine/assets/packed.c
	src/engine/assets/tbl.c)
target_link_libraries(mh_asset_encoding PUBLIC mh_core)
mh_c89(mh_asset_encoding)
mh_check_unsigned_lengths(mh_asset_encoding src/engine/assets/packed.c)

add_library(mh_asset_motion STATIC
	src/engine/assets/route.c
	src/engine/assets/scene.c
	src/engine/assets/scene_decode.c
	src/engine/assets/scene_text.c)
target_link_libraries(mh_asset_motion
	PUBLIC
		mh_navigation_data
		mh_animation
		mh_core
	PRIVATE
		mh_asset_encoding)
mh_c89(mh_asset_motion)

add_library(mh_assets STATIC
	src/engine/assets/ai_path.c
	src/engine/assets/atlas.c
	src/engine/assets/camera_motion.c
	src/engine/assets/config_line.c
	src/engine/assets/car.c
	src/engine/assets/car_paint.c
	src/engine/assets/col.c
	src/engine/assets/col_decode.c
	src/engine/assets/demo.c
	src/engine/assets/env_sound.c
	src/engine/assets/font.c
	src/engine/assets/font_hardware.c
	src/engine/assets/highscore.c
	src/engine/assets/horn.c
	src/engine/assets/ilbm.c
	src/engine/assets/lob.c
	src/engine/assets/material.c
	src/engine/assets/menu_layout.c
	src/engine/assets/divi.c
	src/engine/assets/texture_names.c
	src/engine/assets/myo.c
	src/engine/assets/myo_decode.c
	src/engine/assets/myw_decode.c
	src/engine/assets/myw.c
	src/engine/assets/name_atlas.c
	src/engine/assets/name_images.c
	src/engine/assets/name_mesh.c
	src/engine/assets/object_body.c
	src/engine/assets/palette.c
	src/engine/assets/pdi.c
	src/engine/assets/tga_encode.c
	src/engine/assets/portrait.c
	src/engine/assets/tga_thumbnail.c
	src/engine/assets/tga_portrait.c
	src/engine/assets/profile.c
	src/engine/assets/records.c
	src/engine/assets/remap.c
	src/engine/assets/retro.c
	src/engine/assets/spr.c
	src/engine/assets/spr_decode.c
	src/engine/assets/tga_decode.c
	src/engine/assets/track.c
	src/engine/assets/track_flag.c
	src/engine/assets/wave.c)
if("powervr" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING)
	target_sources(mh_assets PRIVATE src/engine/assets/powervr_name_mesh.c)
endif()
target_include_directories(mh_assets PUBLIC src)
target_link_libraries(mh_assets
	PUBLIC
		mh_retro_parts
		mh_catalog_data
		mh_wireframe
		mh_atlas_map
		mh_audio_pcm
		mh_core
		mh_image
		mh_render_common
		mh_collision_data
		mh_model_data
		mh_world_data
		mh_asset_encoding
		mh_asset_motion
	PRIVATE
		mh_mesh
		mh_image_decoding
		mh_image_collections
		mh_portrait_pixels)
mh_c89(mh_assets)
mh_check_unsigned_lengths(mh_assets
	src/engine/assets/ilbm.c
	src/engine/assets/myo.c
	src/engine/assets/myw.c
	src/engine/assets/pdi.c
	src/engine/assets/tga_decode.c
	src/engine/assets/wave.c)

add_library(mh_catalog_fallback STATIC
	src/backends/catalog/catalog.c
	src/backends/catalog/car_track_definitions.c
	src/backends/catalog/fallback.c
	src/backends/catalog/profile_fallback.c)
target_link_libraries(mh_catalog_fallback
	PUBLIC
		mh_catalog_loading
	PRIVATE
		mh_profile_adp
		mh_demo_asset_layout
		mh_catalog_demo
		mh_assets
		mh_resource_source)
mh_c89(mh_catalog_fallback)

add_library(mh_profile_adp STATIC
	src/backends/catalog/adp_load.c
	src/backends/catalog/adp_source.c)
target_link_libraries(mh_profile_adp
	PUBLIC
		mh_profile_loading
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_profile_adp)

add_library(mh_menu_layout_dta STATIC src/backends/ui/dta_source.c)
target_link_libraries(mh_menu_layout_dta
	PUBLIC
		mh_menu_layout_loading
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_menu_layout_dta)

add_library(mh_retro_parts STATIC src/engine/retro/parts.c)
target_include_directories(mh_retro_parts PUBLIC src)
mh_c89(mh_retro_parts)

add_library(mh_retro_loading STATIC src/engine/retro/load.c)
target_link_libraries(mh_retro_loading PUBLIC
	mh_retro_parts
	mh_error
	mh_resource_source)
mh_c89(mh_retro_loading)

add_library(mh_retro_mhretro STATIC src/backends/retro/mhretro_source.c)
target_link_libraries(mh_retro_mhretro
	PUBLIC
		mh_retro_loading
	PRIVATE
		mh_assets)
mh_c89(mh_retro_mhretro)

add_library(mh_retro_executable_source STATIC
	src/backends/retro/executable_source.c)
target_link_libraries(mh_retro_executable_source
	PUBLIC
		mh_retro_loading
	PRIVATE
		mh_retro_executable)
mh_c89(mh_retro_executable_source)

add_library(mh_retro_fallback STATIC src/backends/retro/fallback.c)
target_link_libraries(mh_retro_fallback
	PUBLIC
		mh_retro_loading
	PRIVATE
		mh_retro_mhretro
		mh_retro_executable_source)
mh_c89(mh_retro_fallback)

add_library(mh_material_mat_srf STATIC
	src/backends/material/mat_srf_load.c
	src/backends/material/mat_srf.c)
target_link_libraries(mh_material_mat_srf
	PUBLIC
		mh_material_loading
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_material_mat_srf)

# The dedicated server selects wrapped routes without camera/AI readers.
add_library(mh_navigation_mot STATIC
	src/backends/navigation/read.c
	src/backends/navigation/route_mot.c)
target_link_libraries(mh_navigation_mot
	PUBLIC
		mh_navigation_loading
	PRIVATE
		mh_asset_motion
		mh_asset_encoding
		mh_resource_source)
mh_c89(mh_navigation_mot)

add_library(mh_navigation_mot_dat STATIC
	src/backends/navigation/mot_dat_source.c
	src/backends/navigation/camera_mot.c
	src/backends/navigation/ai_path_dat.c)
target_link_libraries(mh_navigation_mot_dat
	PUBLIC
		mh_navigation_loading
	PRIVATE
		mh_navigation_mot
		mh_assets)
mh_c89(mh_navigation_mot_dat)

add_library(mh_animation_lightwave STATIC
	src/backends/animation/lightwave_source.c)
target_link_libraries(mh_animation_lightwave
	PUBLIC
		mh_animation
	PRIVATE
		mh_asset_motion
		mh_resource_source)
mh_c89(mh_animation_lightwave)

add_library(mh_collision_col STATIC src/backends/collision/col_decoder.c)
target_link_libraries(mh_collision_col
	PUBLIC
		mh_collision_decoding
	PRIVATE
		mh_assets)
mh_c89(mh_collision_col)

add_library(mh_collision_executable_source STATIC
	src/backends/collision/executable_source.c)
target_link_libraries(mh_collision_executable_source PUBLIC
	mh_collision_executable
	mh_assets
	mh_collision_loading)
mh_c89(mh_collision_executable_source)

add_library(mh_collision_fallback STATIC src/backends/collision/fallback.c)
target_link_libraries(mh_collision_fallback PUBLIC
	mh_collision_col
	mh_collision_executable_source
	mh_assets
	mh_collision_loading)
mh_c89(mh_collision_fallback)

add_library(mh_executable_identity STATIC
	src/engine/assets/executable_identity.c)
target_include_directories(mh_executable_identity PUBLIC src)
mh_c89(mh_executable_identity)

add_library(mh_retro_executable STATIC src/engine/assets/executable_retro.c)
target_link_libraries(mh_retro_executable
	PUBLIC
		mh_assets
	PRIVATE
		mh_executable_identity
		mh_sha256)
mh_c89(mh_retro_executable)

add_library(mh_collision_executable STATIC
	src/engine/assets/executable_collision.c)
target_link_libraries(mh_collision_executable
	PUBLIC
		mh_resource_source
	PRIVATE
		mh_assets
		mh_executable_identity
		mh_sha256
		mh_text)
mh_c89(mh_collision_executable)

add_library(mh_demo_definitions STATIC
	src/engine/assets/demo_car.c
	src/engine/assets/demo_track.c
	src/engine/assets/demo_definition.c
	src/engine/assets/demo_drivers.c
	src/engine/assets/demo_font.c)
target_link_libraries(mh_demo_definitions PUBLIC mh_assets)
mh_c89(mh_demo_definitions)

add_library(mh_demo_asset_layout STATIC src/engine/assets/demo_layout.c)
target_link_libraries(mh_demo_asset_layout PUBLIC mh_content)
mh_c89(mh_demo_asset_layout)

add_library(mh_demo_presentation STATIC
	src/engine/assets/demo_results.c
	src/engine/assets/demo_loading.c)
target_link_libraries(mh_demo_presentation PUBLIC mh_assets mh_resource_source)
mh_c89(mh_demo_presentation)

add_library(mh_catalog_demo STATIC src/backends/catalog/demo_source.c)
target_link_libraries(mh_catalog_demo
	PUBLIC
		mh_catalog_loading
	PRIVATE
		mh_demo_definitions
		mh_demo_asset_layout
		mh_resource_source)
mh_c89(mh_catalog_demo)

add_library(mh_model_myo STATIC src/backends/geometry/myo_decoder.c)
target_link_libraries(mh_model_myo
	PUBLIC
		mh_geometry_decoding
	PRIVATE
		mh_assets)
mh_c89(mh_model_myo)

add_library(mh_wireframe_lob STATIC src/backends/geometry/lob_source.c)
target_link_libraries(mh_wireframe_lob
	PUBLIC
		mh_wireframe
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_wireframe_lob)

add_library(mh_world_myw STATIC src/backends/geometry/myw_decoder.c)
target_link_libraries(mh_world_myw
	PUBLIC
		mh_geometry_decoding
	PRIVATE
		mh_assets)
mh_c89(mh_world_myw)

add_library(mh_asset_loaders STATIC
	src/engine/assets/divi_data.c
	src/engine/assets/mesh_load.c
	src/engine/assets/object_assets.c)
target_include_directories(mh_asset_loaders PUBLIC src)
target_link_libraries(mh_asset_loaders
	PUBLIC
		mh_profile_loading
		mh_horn_loading
		mh_geometry_loading
		mh_collision_loading
		mh_assets
		mh_catalog_loading
	PRIVATE
		mh_image_loading)
mh_c89(mh_asset_loaders)
