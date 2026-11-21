if(MH_BUILD_CLIENT)
	add_executable(mh-finish-render tests/render/finish_signs.c)
	target_link_libraries(mh-finish-render PRIVATE mh_diagnostic_runtime)
	mh_c89(mh-finish-render)

	add_executable(test-sdl-feedback
		tests/unit/test_sdl_feedback.c
		tests/support/sdl_feedback.c)
	target_include_directories(test-sdl-feedback SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	target_link_libraries(test-sdl-feedback PRIVATE mh_error)
	mh_c89(test-sdl-feedback)
	add_test(NAME sdl-force-feedback COMMAND test-sdl-feedback)

	add_executable(mh-clipboard-live tests/integration/clipboard_live.c)
	target_link_libraries(mh-clipboard-live PRIVATE
		mh_diagnostic_input
		mh_diagnostic_graphics_catalog
		SDL3::SDL3)
	target_include_directories(mh-clipboard-live SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-clipboard-live)

	add_executable(test-window-bridges tests/unit/test_window_system.c)
	target_compile_definitions(test-window-bridges PRIVATE
		MH_TEST_SDL_BRIDGES=1)
	target_link_libraries(test-window-bridges PRIVATE mh_diagnostic_runtime)
	mh_c89(test-window-bridges)
	add_test(NAME window-backend-compatibility COMMAND test-window-bridges)

	add_executable(test-optional-capabilities
		tests/integration/optional_capabilities.c
		tests/support/limited_capabilities.c)
	target_link_libraries(test-optional-capabilities PRIVATE
		mh_retro_fallback
		mh_host_sdl
		mh_diagnostic_runtime)

	if(TARGET mh_network_game)
		target_compile_definitions(test-optional-capabilities PRIVATE
			MH_TEST_NETWORK=1)
	endif()
	mh_c89(test-optional-capabilities)
	add_test(NAME optional-capabilities COMMAND test-optional-capabilities)
	set_tests_properties(optional-capabilities PROPERTIES ENVIRONMENT
		"MH_TEST_CAPABILITIES=0")

	add_executable(mh-application-storage
		tests/integration/application_storage.c)
	target_link_libraries(mh-application-storage PRIVATE
		mh_file_read_filesystem
		mh_host_sdl
		mh_storage)
	mh_c89(mh-application-storage)

	add_executable(mh-application-lifecycle
		tests/integration/application_lifecycle.c
		tests/support/input_provider.c
		tests/support/menu_tables.c)
	target_link_libraries(mh-application-lifecycle PRIVATE
		mh_image_png
		mh_retro_fallback
		mh_menu_layout_dta
		mh_palette_act
		mh_catalog_fallback
		mh_horn_fallback
		mh_material_mat_srf
		mh_environment_esd
		mh_collision_fallback
		mh_model_myo
		mh_world_myw
		mh_atlas_remap
		mh_navigation_mot_dat
		mh_animation_lightwave
		mh_wireframe_lob
		mh_host_sdl
		mh_diagnostic_runtime
		mh_user_data_memory)
	# This fixture inspects private application/menu state.
	target_compile_definitions(mh-application-lifecycle PRIVATE
		$<TARGET_PROPERTY:mh_menus_app,COMPILE_DEFINITIONS>)
	mh_c89(mh-application-lifecycle)
	add_test(NAME application-lifecycle COMMAND mh-application-lifecycle)

	add_executable(mh-session-lifecycle
		tests/integration/session_lifecycle.c)
	target_link_libraries(mh-session-lifecycle PRIVATE
		mh_retro_fallback
		mh_menu_layout_dta
		mh_palette_act
		mh_catalog_fallback
		mh_horn_fallback
		mh_material_mat_srf
		mh_environment_esd
		mh_collision_fallback
		mh_model_myo
		mh_world_myw
		mh_atlas_remap
		mh_navigation_mot_dat
		mh_animation_lightwave
		mh_wireframe_lob
		mh_single_session
		mh_demo_session
		mh_league_setup
		mh_network_setup
		mh_user_data_memory
		mh_diagnostic_runtime
		mh_font_fallback
		mh_sprite_spr)

	if(TARGET mh_network_lobby)
		target_link_libraries(mh-session-lifecycle PRIVATE
			mh_network_lobby
			mh_diagnostic_runtime
			mh_font_fallback
			mh_sprite_spr)
		target_compile_definitions(mh-session-lifecycle PRIVATE
			MH_TEST_NETWORK=1)
	endif()
	mh_c89(mh-session-lifecycle)

	add_executable(mh-race-lifecycle
		tests/integration/race_lifecycle.c
		tests/support/input_provider.c)
	target_link_libraries(mh-race-lifecycle PRIVATE
		mh_retro_fallback
		mh_diagnostic_runtime
		mh_user_data_memory)
	mh_c89(mh-race-lifecycle)

	add_executable(mh-software-race tests/integration/software_race.c)
	target_link_libraries(mh-software-race PRIVATE
		mh_retro_fallback
		mh_diagnostic_runtime)
	mh_c89(mh-software-race)

	add_executable(mh-single-driving-assets
		tests/assets/single_driving_assets.c)
	target_link_libraries(mh-single-driving-assets PRIVATE
		mh_single_setup
		mh_diagnostic_runtime)
	mh_c89(mh-single-driving-assets)

	add_executable(mh-cross-track-records
		tests/assets/cross_track_records.c)
	target_link_libraries(mh-cross-track-records PRIVATE
		mh_single_setup
		mh_diagnostic_runtime
		mh_highscore_files)
	mh_c89(mh-cross-track-records)

	add_executable(mh-attract-driving-assets
		tests/assets/attract_driving_assets.c)
	target_link_libraries(mh-attract-driving-assets PRIVATE
		mh_single_setup
		mh_diagnostic_runtime)
	mh_c89(mh-attract-driving-assets)

	add_executable(mh-attract-live tests/integration/attract_live.c)
	target_link_libraries(mh-attract-live PRIVATE
		mh_single_setup
		SDL3::SDL3
		mh_diagnostic_runtime)
	target_include_directories(mh-attract-live SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-attract-live)

	add_executable(mh-local-results-live
		tests/integration/local_results_live.c)
	target_link_libraries(mh-local-results-live PRIVATE
		mh_single_setup
		SDL3::SDL3
		mh_diagnostic_runtime)
	target_include_directories(mh-local-results-live SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-local-results-live)

	add_executable(mh-software-scene-assets
		tests/assets/software_scene_assets.c)
	target_link_libraries(mh-software-scene-assets PRIVATE
		mh_catalog_fallback
		mh_horn_fallback
		mh_material_mat_srf
		mh_environment_esd
		mh_collision_fallback
		mh_model_myo
		mh_world_myw
		mh_atlas_remap
		mh_navigation_mot_dat
		mh_animation_lightwave
		mh_wireframe_lob
		mh_diagnostic_runtime
		mh_render_software)
	mh_c89(mh-software-scene-assets)

	add_executable(mh-results-profile-assets
		tests/assets/results_profile_assets.c
		tests/support/input_provider.c)
	target_link_libraries(mh-results-profile-assets PRIVATE
		mh_file_read_filesystem
		mh_league_setup
		mh_diagnostic_runtime)
	mh_c89(mh-results-profile-assets)

	add_executable(mh-league-campaign tests/assets/league_campaign.c)
	target_link_libraries(mh-league-campaign PRIVATE
		mh_catalog_fallback
		mh_league_setup
		mh_diagnostic_runtime
		mh_leagues)
	mh_c89(mh-league-campaign)

	add_executable(mh-league-driving-assets
		tests/assets/league_driving_assets.c)
	target_link_libraries(mh-league-driving-assets PRIVATE
		mh_league_setup
		mh_diagnostic_runtime)
	mh_c89(mh-league-driving-assets)

	add_executable(mh-net-race-check tests/assets/network_race_assets.c)
	target_link_libraries(mh-net-race-check PRIVATE
		mh_network_setup
		mh_diagnostic_runtime)
	mh_c89(mh-net-race-check)

	add_executable(mh-network-frame-assets
		tests/assets/network_frame_assets.c)
	target_link_libraries(mh-network-frame-assets PRIVATE
		mh_network_setup
		mh_demo_sim
		mh_diagnostic_runtime)
	mh_c89(mh-network-frame-assets)

	if(TARGET mh_diagnostic_network)
		add_executable(mh-lobby-clipboard
			tests/integration/lobby_clipboard.c)
		target_link_libraries(mh-lobby-clipboard PRIVATE
			mh_horn_fallback
			mh_catalog_fallback
			mh_diagnostic_network
			mh_network_lobby
			SDL3::SDL3
			mh_diagnostic_runtime)
		target_include_directories(mh-lobby-clipboard SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-lobby-clipboard)

		add_executable(mh-lobby-frame-live
			tests/integration/lobby_frame_live.c
			tests/support/input_provider.c)
		target_link_libraries(mh-lobby-frame-live PRIVATE
			mh_horn_fallback
			mh_catalog_fallback
			mh_diagnostic_network
			mh_network_lobby
			SDL3::SDL3
			mh_diagnostic_runtime)
		target_include_directories(mh-lobby-frame-live SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-lobby-frame-live)
	endif()

	add_executable(mh-shadow-assets tests/assets/shadow_assets.c)
	target_link_libraries(mh-shadow-assets PRIVATE mh_diagnostic_runtime)
	mh_c89(mh-shadow-assets)

	add_executable(mh-lod-assets tests/assets/lod_assets.c)
	target_link_libraries(mh-lod-assets PRIVATE
		mh_render_sdl_gpu
		mh_file_read_filesystem
		mh_diagnostic_runtime
		SDL3::SDL3
		mh_image_fallback
		mh_image_pdi)
	target_include_directories(mh-lod-assets SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-lod-assets)

	add_executable(mh-name-assets tests/assets/name_assets.c)
	target_link_libraries(mh-name-assets PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-name-assets SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-name-assets)

	add_executable(mh-glenz-lifecycle tests/integration/glenz_lifecycle.c)
	target_link_libraries(mh-glenz-lifecycle PRIVATE mh_diagnostic_runtime)
	mh_c89(mh-glenz-lifecycle)

	add_executable(mh-settings-store-assets
		tests/assets/settings_store_assets.c)
	target_link_libraries(mh-settings-store-assets PRIVATE
		mh_catalog_fallback
		mh_diagnostic_runtime)
	mh_c89(mh-settings-store-assets)

	add_executable(mh-attached-halo-assets tests/assets/attached_halos.c)
	target_link_libraries(mh-attached-halo-assets PRIVATE
		mh_diagnostic_runtime)
	mh_c89(mh-attached-halo-assets)

	add_executable(mh-object-visibility-render
		tests/render/object_visibility_render.c)
	target_link_libraries(mh-object-visibility-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-object-visibility-render SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-object-visibility-render)

	add_executable(mh-scene-order-render tests/render/scene_order_render.c)
	target_link_libraries(mh-scene-order-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-scene-order-render SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-scene-order-render)

	if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
		CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")

		add_executable(mh-atlas-render tests/render/atlas_render.c)
		target_link_libraries(mh-atlas-render PRIVATE
			mh_render_sdl_gpu
			mh_diagnostic_runtime
			SDL3::SDL3)
		target_include_directories(mh-atlas-render SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-atlas-render)
	endif()

	add_executable(mh-atmosphere-render tests/render/atmosphere_render.c)
	target_link_libraries(mh-atmosphere-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime)
	target_include_directories(mh-atmosphere-render SYSTEM PRIVATE
		$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>)
	mh_c89(mh-atmosphere-render)

	add_executable(mh-motion-blur-render tests/render/motion_blur_render.c)
	target_link_libraries(mh-motion-blur-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-motion-blur-render SYSTEM PRIVATE
		$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>)
	mh_c89(mh-motion-blur-render)

	add_executable(mh-viewport-render tests/render/viewport_render.c)
	target_link_libraries(mh-viewport-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-viewport-render SYSTEM PRIVATE
		$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>)
	mh_c89(mh-viewport-render)

	add_executable(mh-tron-render
		tests/render/tron_render.c
		tests/support/tron_render.c)
	target_link_libraries(mh-tron-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-tron-render SYSTEM PRIVATE
		$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>)
	mh_c89(mh-tron-render)

	add_executable(mh-retro-render tests/render/retro_render.c)
	target_link_libraries(mh-retro-render PRIVATE
		mh_render_sdl_gpu
		mh_retro_fallback
		mh_resource_filesystem
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-retro-render SYSTEM PRIVATE
		$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
	mh_c89(mh-retro-render)

	add_executable(mh-hidden-weather-render
		tests/render/hidden_weather_render.c)
	target_link_libraries(mh-hidden-weather-render PRIVATE
		mh_render_sdl_gpu
		mh_file_read_filesystem
		mh_diagnostic_runtime)
	target_include_directories(mh-hidden-weather-render SYSTEM PRIVATE
		$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>)
	mh_c89(mh-hidden-weather-render)

	add_executable(mh-underwater-render tests/render/underwater_render.c)
	target_link_libraries(mh-underwater-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime)
	target_include_directories(mh-underwater-render SYSTEM PRIVATE
		$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>)
	mh_c89(mh-underwater-render)

	add_executable(mh-fog-render tests/render/fog_render.c)
	target_link_libraries(mh-fog-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime)
	target_include_directories(mh-fog-render SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-fog-render)

	add_executable(mh-shadow-render tests/render/shadow_render.c)
	target_link_libraries(mh-shadow-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime)
	target_include_directories(mh-shadow-render SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-shadow-render)

	add_executable(mh-material-render tests/render/material_render.c)
	target_link_libraries(mh-material-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime)
	target_include_directories(mh-material-render SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-material-render)

	add_executable(mh-skids-assets tests/assets/skids_assets.c)
	target_link_libraries(mh-skids-assets PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime)
	target_include_directories(mh-skids-assets SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-skids-assets)

	add_executable(mh-particle-render tests/render/particle_render.c)
	target_link_libraries(mh-particle-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime)
	target_include_directories(mh-particle-render SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-particle-render)

	add_executable(mh-cheat-assets tests/assets/cheat_assets.c)
	target_link_libraries(mh-cheat-assets PRIVATE mh_diagnostic_runtime)
	mh_c89(mh-cheat-assets)

	add_executable(mh-glenz-render tests/render/glenz_render.c)
	target_link_libraries(mh-glenz-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-glenz-render SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-glenz-render)

	add_executable(mh-direct3d-sampling tests/render/direct3d_sampling.c)
	target_link_libraries(mh-direct3d-sampling PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-direct3d-sampling SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-direct3d-sampling)

	add_executable(mh-direct3d-materials tests/render/direct3d_materials.c)
	target_link_libraries(mh-direct3d-materials PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-direct3d-materials SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-direct3d-materials)

	add_executable(mh-direct3d-effects tests/render/direct3d_effects.c)
	target_link_libraries(mh-direct3d-effects PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-direct3d-effects SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-direct3d-effects)

	add_executable(mh-direct3d-skids tests/render/direct3d_skids.c)
	target_link_libraries(mh-direct3d-skids PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-direct3d-skids SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-direct3d-skids)

	if("powervr" IN_LIST MH_RENDER_PROFILES)
		add_executable(mh-powervr-scene tests/render/powervr_scene.c)
		target_link_libraries(mh-powervr-scene PRIVATE
			mh_render_sdl_gpu
			mh_mesh
			SDL3::SDL3)
		target_include_directories(mh-powervr-scene SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-powervr-scene)

		add_executable(mh-powervr-assets tests/render/powervr_assets.c)
		target_link_libraries(mh-powervr-assets PRIVATE
			mh_render_sdl_gpu
			mh_diagnostic_runtime
			SDL3::SDL3)
		target_include_directories(mh-powervr-assets SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-powervr-assets)

		add_executable(mh-powervr-sky tests/render/powervr_sky.c)
		target_link_libraries(mh-powervr-sky PRIVATE
			mh_render_sdl_gpu
			SDL3::SDL3)
		target_include_directories(mh-powervr-sky SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-powervr-sky)

		add_executable(mh-powervr-skids tests/render/powervr_skids.c)
		target_link_libraries(mh-powervr-skids PRIVATE
			mh_render_sdl_gpu
			SDL3::SDL3)
		target_include_directories(mh-powervr-skids SYSTEM PRIVATE
			$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
		mh_c89(mh-powervr-skids)

		add_executable(mh-powervr-unavailable
			tests/render/powervr_unavailable.c)
		target_link_libraries(mh-powervr-unavailable PRIVATE
			mh_render_sdl_gpu
			SDL3::SDL3)
		target_include_directories(mh-powervr-unavailable SYSTEM PRIVATE
			$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
		mh_c89(mh-powervr-unavailable)

		add_executable(mh-powervr-particles tests/render/powervr_particles.c)
		target_link_libraries(mh-powervr-particles PRIVATE
			mh_render_sdl_gpu
			SDL3::SDL3)
		target_include_directories(mh-powervr-particles SYSTEM PRIVATE
			$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
		mh_c89(mh-powervr-particles)

		add_executable(mh-powervr-halos tests/render/powervr_halos.c)
		target_link_libraries(mh-powervr-halos PRIVATE
			mh_render_sdl_gpu
			SDL3::SDL3)
		target_include_directories(mh-powervr-halos SYSTEM PRIVATE
			$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
		mh_c89(mh-powervr-halos)

		add_executable(mh-powervr-shadows tests/render/powervr_shadows.c)
		target_link_libraries(mh-powervr-shadows PRIVATE
			mh_render_sdl_gpu
			SDL3::SDL3)
		target_include_directories(mh-powervr-shadows SYSTEM PRIVATE
			$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
		mh_c89(mh-powervr-shadows)
	endif()

	add_executable(mh-direct3d-faces tests/render/direct3d_faces.c)
	target_link_libraries(mh-direct3d-faces PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-direct3d-faces SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-direct3d-faces)

	add_executable(mh-presentation tests/render/presentation.c)
	target_link_libraries(mh-presentation PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-presentation SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-presentation)

	add_executable(mh-texture-filter tests/render/texture_filter.c)
	target_link_libraries(mh-texture-filter PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-texture-filter SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-texture-filter)

	add_executable(mh-glenz-depth tests/render/glenz_depth.c)
	target_link_libraries(mh-glenz-depth PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-glenz-depth SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-glenz-depth)

	add_executable(mh-unavailable-render tests/render/unavailable_render.c)
	target_link_libraries(mh-unavailable-render PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-unavailable-render SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-unavailable-render)

	add_executable(mh-hud-secrets-assets tests/assets/hud_secrets_assets.c)
	target_link_libraries(mh-hud-secrets-assets PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-hud-secrets-assets SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-hud-secrets-assets)

	add_executable(mh-pause-assets tests/assets/pause_assets.c)
	target_link_libraries(mh-pause-assets PRIVATE
		mh_render_sdl_gpu
		mh_diagnostic_runtime
		SDL3::SDL3)
	target_include_directories(mh-pause-assets SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-pause-assets)

	if(UNIX)
		add_executable(mh-pause-live tests/integration/pause_live.c)
		target_link_libraries(mh-pause-live PRIVATE
			mh_diagnostic_runtime
			SDL3::SDL3)
		target_include_directories(mh-pause-live SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-pause-live)

		add_executable(mh-process-clock-live
			tests/integration/process_clock_live.c)
		target_link_libraries(mh-process-clock-live PRIVATE
			mh_diagnostic_runtime
			SDL3::SDL3)
		target_include_directories(mh-process-clock-live SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-process-clock-live)

		if("mouse" IN_LIST MH_INPUT_DEVICES AND
			"joystick" IN_LIST MH_INPUT_DEVICES AND
			MH_INPUT_FEEDBACK)

			add_executable(mh-devices-live
				tests/integration/devices_live.c)
			target_link_libraries(mh-devices-live PRIVATE
				mh_diagnostic_runtime
				SDL3::SDL3)
			target_include_directories(mh-devices-live SYSTEM
				PRIVATE
				"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
			mh_c89(mh-devices-live)
		endif()

		add_executable(mh-view-controls-live
			tests/integration/view_controls_live.c)
		target_link_libraries(mh-view-controls-live PRIVATE
			mh_diagnostic_runtime
			SDL3::SDL3)
		target_include_directories(mh-view-controls-live SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-view-controls-live)

		add_executable(mh-bindings-live
			tests/integration/bindings_live.c)
		target_link_libraries(mh-bindings-live PRIVATE
			mh_diagnostic_runtime
			SDL3::SDL3)
		target_include_directories(mh-bindings-live SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-bindings-live)
	endif()

	if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
		CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")

		target_link_options(mh-atlas-render PRIVATE
			"-Wl,--wrap=SDL_CreateGPUTexture"
			"-Wl,--wrap=SDL_ReleaseGPUTexture")

	endif()
endif()

add_executable(test-mipmap tests/unit/test_mipmap.c)
target_link_libraries(test-mipmap PRIVATE mh_render_common)
mh_c89(test-mipmap)
add_test(NAME texture-mipmap COMMAND test-mipmap)
