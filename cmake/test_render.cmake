if(BUILD_TESTING)
	add_executable(test-openmh-light-buffer tests/unit/test_openmh_light_buffer.c)
	target_link_libraries(test-openmh-light-buffer PRIVATE mh_assets)
	mh_c89(test-openmh-light-buffer)
	add_test(NAME openmh-light-buffer COMMAND test-openmh-light-buffer)

	add_executable(test-object-light tests/unit/test_object_light.c)
	target_link_libraries(test-object-light PRIVATE mh_render_common)
	mh_c89(test-object-light)
	add_test(NAME object-light COMMAND test-object-light)

	add_executable(test-shadow tests/unit/test_shadow.c)
	target_link_libraries(test-shadow PRIVATE mh_assets)
	mh_c89(test-shadow)
	add_test(NAME car-shadow COMMAND test-shadow)

	add_executable(test-fog tests/unit/test_fog.c)
	target_link_libraries(test-fog PRIVATE mh_assets)
	mh_c89(test-fog)
	add_test(NAME hardware-fog COMMAND test-fog)
endif()

if(BUILD_TESTING)
	add_executable(test-atmosphere tests/unit/test_atmosphere.c)
	target_link_libraries(test-atmosphere PRIVATE
		mh_atmosphere_mesh
		mh_image_fallback)
	mh_c89(test-atmosphere)
	add_test(NAME sky-and-halos COMMAND test-atmosphere)
endif()

add_executable(mh-software-scene tests/render/software_scene.c)
target_link_libraries(mh-software-scene PRIVATE mh_render_software)
mh_c89(mh-software-scene)

if(BUILD_TESTING)
	add_test(NAME software-renderer COMMAND mh-software-scene)
endif()

add_executable(mh-software-palette tests/render/software_palette.c)
target_link_libraries(mh-software-palette PRIVATE mh_software_raster)
mh_c89(mh-software-palette)

if(BUILD_TESTING)
	add_test(NAME software-palette-guards COMMAND mh-software-palette)
endif()

add_executable(mh-software-texture tests/render/software_texture.c)
target_link_libraries(mh-software-texture PRIVATE mh_software_raster)
mh_c89(mh-software-texture)

if(BUILD_TESTING)
	add_test(NAME software-texture-guards COMMAND mh-software-texture)
endif()

add_executable(mh-software-indexed tests/render/software_indexed.c)
target_link_libraries(mh-software-indexed PRIVATE
	mh_file_read_filesystem
	mh_render_software
	mh_image_fallback
	mh_image_pdi)
mh_c89(mh-software-indexed)

if(BUILD_TESTING)
	add_test(NAME software-indexed-assets COMMAND mh-software-indexed)
endif()

add_executable(mh-software-near-clip tests/render/software_near_clip.c)
target_link_libraries(mh-software-near-clip PRIVATE mh_software_raster)
mh_c89(mh-software-near-clip)

if(BUILD_TESTING)
	add_test(NAME software-near-clip COMMAND mh-software-near-clip)
endif()

add_executable(mh-software-faces tests/render/software_faces.c)
target_link_libraries(mh-software-faces PRIVATE mh_software_raster)
mh_c89(mh-software-faces)

if(BUILD_TESTING)
	add_test(NAME software-faces COMMAND mh-software-faces)
endif()

add_executable(mh-software-projection tests/render/software_projection.c)
target_link_libraries(mh-software-projection PRIVATE
	mh_software_raster
	mh_render_common)
mh_c89(mh-software-projection)

if(BUILD_TESTING)
	add_test(NAME software-projection COMMAND mh-software-projection)
endif()

add_executable(mh-software-order tests/render/software_order.c)
target_link_libraries(mh-software-order PRIVATE mh_software_raster)
mh_c89(mh-software-order)

if(BUILD_TESTING)
	add_test(NAME software-order COMMAND mh-software-order)
endif()

add_executable(mh-software-fog tests/render/software_fog.c)
target_link_libraries(mh-software-fog PRIVATE mh_software_raster)
mh_c89(mh-software-fog)

if(BUILD_TESTING)
	add_test(NAME software-fog COMMAND mh-software-fog)
endif()

add_executable(mh-software-lens tests/render/software_lens.c)
target_link_libraries(mh-software-lens PRIVATE
	mh_render_software
	mh_render_common)
mh_c89(mh-software-lens)

if(BUILD_TESTING)
	add_test(NAME software-lens-scene COMMAND mh-software-lens)
endif()

add_executable(mh-software-sky-image tests/render/software_sky_image.c)
target_link_libraries(mh-software-sky-image PRIVATE mh_software_raster)
mh_c89(mh-software-sky-image)

if(BUILD_TESTING)
	add_test(NAME software-sky-image COMMAND mh-software-sky-image)
endif()

add_executable(mh-software-sky tests/render/software_sky.c)
target_link_libraries(mh-software-sky PRIVATE mh_software_raster)
mh_c89(mh-software-sky)

if(BUILD_TESTING)
	add_test(NAME software-sky COMMAND mh-software-sky)
endif()

add_executable(mh-software-panorama tests/render/software_panorama.c)
target_link_libraries(mh-software-panorama PRIVATE mh_render_software)
mh_c89(mh-software-panorama)

if(BUILD_TESTING)
	add_test(NAME software-panorama COMMAND mh-software-panorama)
endif()

add_executable(mh-software-material tests/render/software_material.c)
target_link_libraries(mh-software-material PRIVATE
	mh_file_read_filesystem
	mh_assets
	mh_image_fallback
	mh_image_pdi)
mh_c89(mh-software-material)

if(BUILD_TESTING)
	add_test(NAME software-material COMMAND mh-software-material)
endif()

add_executable(mh-software-lighting tests/render/software_lighting.c)
target_link_libraries(mh-software-lighting PRIVATE mh_software_raster)
mh_c89(mh-software-lighting)

if(BUILD_TESTING)
	add_test(NAME software-lighting COMMAND mh-software-lighting)
endif()

add_executable(mh-software-reflection tests/render/software_reflection.c)
target_link_libraries(mh-software-reflection PRIVATE
	mh_software_raster
	mh_render_common)
mh_c89(mh-software-reflection)

if(BUILD_TESTING)
	add_test(NAME software-reflection COMMAND mh-software-reflection)
endif()

add_executable(mh-software-light-select tests/render/software_light_select.c)
target_link_libraries(mh-software-light-select PRIVATE
	mh_file_read_filesystem
	mh_render_software)
mh_c89(mh-software-light-select)

if(BUILD_TESTING)
	add_test(NAME software-light-select COMMAND mh-software-light-select)
endif()

add_executable(mh-software-scenery tests/render/software_scenery.c)
target_link_libraries(mh-software-scenery PRIVATE
	mh_file_read_filesystem
	mh_render_software
	mh_image_fallback
	mh_image_pdi)
mh_c89(mh-software-scenery)

if(BUILD_TESTING)
	add_test(NAME software-scenery COMMAND mh-software-scenery)
endif()

add_executable(mh-software-directional tests/render/software_directional.c)
target_link_libraries(mh-software-directional PRIVATE
	mh_file_read_filesystem
	mh_assets)
mh_c89(mh-software-directional)

if(BUILD_TESTING)
	add_test(NAME software-directional COMMAND mh-software-directional)
endif()

add_executable(mh-software-car-lighting tests/render/software_car_lighting.c)
target_link_libraries(mh-software-car-lighting PRIVATE
	mh_render_software
	mh_scene_models)
mh_c89(mh-software-car-lighting)

if(BUILD_TESTING)
	add_test(NAME software-car-lighting COMMAND mh-software-car-lighting)
endif()

add_executable(mh-software-cars tests/render/software_cars.c)
target_link_libraries(mh-software-cars PRIVATE
	mh_file_read_filesystem
	mh_render_software
	mh_scene_models
	mh_image_fallback
	mh_image_pdi)
mh_c89(mh-software-cars)

add_executable(mh-software-ui tests/render/software_ui.c)
target_link_libraries(mh-software-ui PRIVATE
	mh_file_read_filesystem
	mh_render_software
	mh_ui)
mh_c89(mh-software-ui)

if(BUILD_TESTING)
	add_test(NAME software-ui COMMAND mh-software-ui)
endif()

add_executable(mh-software-text tests/render/software_text.c)
target_link_libraries(mh-software-text PRIVATE
	mh_file_read_filesystem
	mh_render_software
	mh_ui)
mh_c89(mh-software-text)

if(BUILD_TESTING)
	add_test(NAME software-text COMMAND mh-software-text)
endif()

add_executable(mh-software-skid tests/render/software_skid.c)
target_link_libraries(mh-software-skid PRIVATE mh_render_common)
mh_c89(mh-software-skid)

if(BUILD_TESTING)
	add_test(NAME software-skid COMMAND mh-software-skid)
endif()

add_executable(mh-software-skid-light tests/render/software_skid_light.c)
target_link_libraries(mh-software-skid-light PRIVATE mh_software_raster)
mh_c89(mh-software-skid-light)

if(BUILD_TESTING)
	add_test(NAME software-skid-light COMMAND mh-software-skid-light)
endif()

add_executable(mh-software-skid-face tests/render/software_skid_face.c)
target_link_libraries(mh-software-skid-face PRIVATE
	mh_software_raster
	mh_render_common)
mh_c89(mh-software-skid-face)

if(BUILD_TESTING)
	add_test(NAME software-skid-face COMMAND mh-software-skid-face)
endif()

add_executable(mh-software-skid-history tests/render/software_skid_history.c)
target_link_libraries(mh-software-skid-history PRIVATE mh_render_common)
mh_c89(mh-software-skid-history)

if(BUILD_TESTING)
	add_test(NAME software-skid-history COMMAND mh-software-skid-history)
endif()

add_executable(mh-software-skids tests/render/software_skids.c)
target_link_libraries(mh-software-skids PRIVATE
	mh_file_read_filesystem
	mh_render_software)
mh_c89(mh-software-skids)

if(BUILD_TESTING)
	add_test(NAME software-skids COMMAND mh-software-skids)
endif()

add_executable(mh-software-skid-scene tests/render/software_skid_scene.c)
target_link_libraries(mh-software-skid-scene PRIVATE mh_render_software)
mh_c89(mh-software-skid-scene)

if(BUILD_TESTING)
	add_test(NAME software-skid-scene COMMAND mh-software-skid-scene)
endif()

add_executable(mh-software-shadow tests/render/software_shadow.c)
target_link_libraries(mh-software-shadow PRIVATE mh_render_common)
mh_c89(mh-software-shadow)

if(BUILD_TESTING)
	add_test(NAME software-shadow COMMAND mh-software-shadow)
endif()

add_executable(mh-software-shadow-mesh tests/render/software_shadow_mesh.c)
target_link_libraries(mh-software-shadow-mesh PRIVATE
	mh_effect_mesh
	mh_render_software)
mh_c89(mh-software-shadow-mesh)

if(BUILD_TESTING)
	add_test(NAME software-shadow-mesh COMMAND mh-software-shadow-mesh)
endif()

add_executable(mh-software-particles tests/render/software_particles.c)
target_link_libraries(mh-software-particles PRIVATE mh_render_software)
mh_c89(mh-software-particles)

if(BUILD_TESTING)
	add_test(NAME software-particles COMMAND mh-software-particles)
endif()

add_executable(mh-software-names tests/render/software_names.c)
target_link_libraries(mh-software-names PRIVATE
	mh_file_read_filesystem
	mh_render_software)
mh_c89(mh-software-names)

if(BUILD_TESTING)
	add_test(NAME software-names COMMAND mh-software-names)
endif()

add_executable(mh-software-halo-visibility
	tests/render/software_halo_visibility.c)
target_link_libraries(mh-software-halo-visibility PRIVATE mh_software_raster)
mh_c89(mh-software-halo-visibility)

if(BUILD_TESTING)
	add_test(NAME software-halo-visibility COMMAND
		mh-software-halo-visibility)
endif()

add_executable(mh-software-halo-geometry tests/render/software_halo_geometry.c)
target_link_libraries(mh-software-halo-geometry PRIVATE
	mh_software_raster
	mh_render_common)
mh_c89(mh-software-halo-geometry)

if(BUILD_TESTING)
	add_test(NAME software-halo-geometry COMMAND mh-software-halo-geometry)
endif()

add_executable(mh-software-halo-scene tests/render/software_halo_scene.c)
target_link_libraries(mh-software-halo-scene PRIVATE mh_render_software)
mh_c89(mh-software-halo-scene)

if(BUILD_TESTING)
	add_test(NAME software-halo-scene COMMAND mh-software-halo-scene)
endif()

add_executable(mh-software-blur tests/render/software_blur.c)
target_link_libraries(mh-software-blur PRIVATE mh_render_software mh_ui)
mh_c89(mh-software-blur)

if(BUILD_TESTING)
	add_test(NAME software-blur COMMAND mh-software-blur)
endif()

if(MH_BUILD_CLIENT)
	add_executable(mh-shader-contract tests/render/shader_contract.c)
	target_link_libraries(mh-shader-contract PRIVATE
		mh_render_build
		mh_render_sdl_gpu
		SDL3::SDL3)
	target_include_directories(mh-shader-contract SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-shader-contract)

	if(BUILD_TESTING)
		add_test(NAME shader-contract COMMAND mh-shader-contract
			--check)
	endif()

	add_executable(mh-software-window tests/render/software_window.c)
	target_link_libraries(mh-software-window PRIVATE
		mh_render_software
		mh_ui
		mh_canvas_sdl
		mh_window_sdl
		mh_input_sdl
		mh_image_capture
		SDL3::SDL3)
	target_include_directories(mh-software-window SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-software-window)
endif()

add_executable(mh-software-present-failure
	tests/render/software_blur.c
	tests/support/software_presentation.c)
target_compile_definitions(mh-software-present-failure PRIVATE
	MH_TEST_PRESENT_FAILURE=1)
target_link_libraries(mh-software-present-failure PRIVATE
	mh_render_software
	mh_ui
	mh_window_system)
mh_c89(mh-software-present-failure)

if(BUILD_TESTING)
	add_test(NAME software-present-failure COMMAND
		mh-software-present-failure)
endif()

if(BUILD_TESTING)
	add_executable(test-glenz-scene tests/unit/test_glenz_scene.c)
	target_link_libraries(test-glenz-scene PRIVATE mh_race_scene)
	mh_c89(test-glenz-scene)
	add_test(NAME glenz-scene COMMAND test-glenz-scene)
endif()

add_executable(mh-direct3d-color-check tests/render/direct3d_color.c)
target_link_libraries(mh-direct3d-color-check PRIVATE mh_render_common)
mh_c89(mh-direct3d-color-check)

if(BUILD_TESTING)
	add_test(NAME direct3d-material-color COMMAND mh-direct3d-color-check)
endif()

add_executable(mh-powervr-effect-check tests/render/powervr_effect.c)
target_link_libraries(mh-powervr-effect-check PRIVATE mh_render_common)
mh_c89(mh-powervr-effect-check)

if(BUILD_TESTING)
	add_test(NAME powervr-effect COMMAND mh-powervr-effect-check)
endif()

add_executable(mh-powervr-texture-check tests/render/powervr_texture.c)
target_link_libraries(mh-powervr-texture-check PRIVATE mh_render_common)
mh_c89(mh-powervr-texture-check)

if(BUILD_TESTING)
	add_test(NAME powervr-texture COMMAND mh-powervr-texture-check)
endif()

if(BUILD_TESTING AND TARGET mh_render_sdl_gpu)
	add_executable(mh-openmh-render-check tests/render/openmh_lighting.c)
	target_link_libraries(mh-openmh-render-check PRIVATE
		mh_render_sdl_gpu
		mh_window_sdl)
	mh_c89(mh-openmh-render-check)
endif()
