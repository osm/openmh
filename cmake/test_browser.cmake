# Run the same fixtures as native builds through the SDK's Node Wasm runtime.
# These check portable engine behavior; browser acceptance checks run separately.
add_custom_target(check-wasm-core
	COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -L wasm-core
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
	USES_TERMINAL)

foreach(check IN ITEMS "test-tick-clock;simulation-tick-clock"
	"test-tick-input;simulation-tick-input"
	"test-fixed-frame;simulation-fixed-frame"
	"test-scene-interpolation;simulation-scene-interpolation"
	"test-engine;engine-gearbox" "test-grip;surface-grip"
	"test-suspension-cache;suspension-plane-cache"
	"test-suspension-forces;suspension-force-stores"
	"test-motion-stores;motion-translation-stores"
	"test-forces;vehicle-forces"
	"test-material-face;material-face-preparation"
	"test-powervr-order;powervr-region-order" "test-pair;car-pair-contact"
	"test-recovery;automatic-recovery" "test-collision;collision-contact"
	"test-menu-configuration;menu-feature-configuration"
	"test-data-file-native;stored-file-native"
	"test-png-encoder;png-encoder")
	list(GET check 0 executable)
	list(GET check 1 test)
	add_dependencies(check-wasm-core ${executable})
	set_tests_properties(${test} PROPERTIES LABELS wasm-core TIMEOUT 30)
endforeach()

add_executable(mh-webgl-pipelines-check tests/browser/pipelines.c)
target_link_libraries(mh-webgl-pipelines-check PRIVATE mh_render_webgl)
target_link_options(mh-webgl-pipelines-check PRIVATE
	-sMODULARIZE=1
	-sEXPORT_NAME=createPipelineCheck
	-sINVOKE_RUN=0
	"-sEXPORTED_FUNCTIONS=['_main','_mh_webgl_pipeline_check']"
	-sMIN_WEBGL_VERSION=2
	-sMAX_WEBGL_VERSION=2)
mh_c89(mh-webgl-pipelines-check)

if("webgl" IN_LIST shader_targets)
	add_executable(mh-webgl-history-check tests/browser/history.c)
	target_link_libraries(mh-webgl-history-check PRIVATE mh_render_webgl)
	target_link_options(mh-webgl-history-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createHistoryCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_FUNCTIONS=['_main','_mh_webgl_history_check']"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-history-check)

	add_executable(mh-webgl-depth-check tests/browser/depth.c)
	target_link_libraries(mh-webgl-depth-check PRIVATE mh_render_webgl)
	string(CONCAT browser_exported_functions
		"-sEXPORTED_FUNCTIONS=['_main',"
		"'_mh_webgl_depth_check',"
		"'_mh_webgl_depth_poll_check']")
	target_link_options(mh-webgl-depth-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createDepthCheck
		-sINVOKE_RUN=0
		"${browser_exported_functions}"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-depth-check)

	add_executable(mh-webgl-scenes-check
		tests/browser/scenes.c
		tests/browser/powervr_order.c)
	target_link_libraries(mh-webgl-scenes-check PRIVATE mh_render_webgl)
	target_link_options(mh-webgl-scenes-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createSceneCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_FUNCTIONS=['_main','_mh_webgl_scene_check']"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-scenes-check)

	add_executable(mh-webgl-meshes-check tests/browser/meshes.c)
	target_link_libraries(mh-webgl-meshes-check PRIVATE mh_render_webgl)
	target_link_options(mh-webgl-meshes-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createMeshCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_FUNCTIONS=['_main','_mh_webgl_mesh_check']"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-meshes-check)

	add_executable(mh-webgl-programs-check
		tests/browser/programs.c
		tests/browser/vertices.c)
	target_include_directories(mh-webgl-programs-check PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(mh-webgl-programs-check PRIVATE mh_render_webgl)
	target_link_options(mh-webgl-programs-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createProgramCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_FUNCTIONS=['_main','_mh_webgl_program_check']"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-programs-check)
endif()

if(TARGET mh_render_webgl_window)
	add_executable(mh-browser-audio-queue-check tests/browser/audio_queue.c)
	target_link_libraries(mh-browser-audio-queue-check PRIVATE mh_audio_openal)
	string(CONCAT browser_exported_functions
		"-sEXPORTED_FUNCTIONS=['_main',"
		"'_mh_browser_audio_queue',"
		"'_mh_browser_audio_level',"
		"'_mh_browser_audio_update']")
	target_link_options(mh-browser-audio-queue-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createAudioQueueCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_RUNTIME_METHODS=['ccall']"
		"${browser_exported_functions}")
	mh_c89(mh-browser-audio-queue-check)

	add_executable(mh-browser-audio-suspend-check tests/browser/audio_suspend.c)
	target_link_libraries(mh-browser-audio-suspend-check PRIVATE
		mh_audio_sdl
		mh_audio_openal)
	target_link_options(mh-browser-audio-suspend-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createAudioSuspendCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_RUNTIME_METHODS=['ccall']"
		"-sEXPORTED_FUNCTIONS=['_main','_mh_browser_audio_suspend']")
	mh_c89(mh-browser-audio-suspend-check)

	add_executable(mh-webgl-canvas-recovery-check
		tests/browser/canvas_recovery.c)
	target_link_libraries(mh-webgl-canvas-recovery-check PRIVATE
		mh_canvas_webgl
		mh_window_sdl)
	target_link_options(mh-webgl-canvas-recovery-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createRecoveryCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_FUNCTIONS=['_main','_mh_webgl_canvas_recovery']"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-canvas-recovery-check)

	# Same application, with an AI driver and observation confined to this target.
	mh_browser_application(mh-browser-single-player-check)
	target_sources(mh-browser-single-player-check PRIVATE
		tests/browser/single_player.c
		tests/browser/league.c
		tests/browser/league_fixture.c)
	target_link_options(mh-browser-single-player-check PRIVATE
		-Wl,--wrap=mh_menu_step
		-Wl,--wrap=mh_race_loop_open
		-Wl,--wrap=mh_race_loop_step
		-Wl,--wrap=mh_race_loop_close
		-Wl,--wrap=mh_application_set_suspended
		-Wl,--wrap=mh_video_decoder_read
		-Wl,--wrap=mh_season_screen_step
		"SHELL:--js-library '${CMAKE_CURRENT_SOURCE_DIR}/tests/browser/single_player_bridge.js'")
	set_property(TARGET mh-browser-single-player-check APPEND PROPERTY
		LINK_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/tests/browser/single_player_bridge.js")

	# Export test hooks separately; production packages never include them.
	add_custom_target(wasm-runtime-tests
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_SOURCE_DIR}/tools/package_browser_runtime.py"
			--build "${CMAKE_BINARY_DIR}" --output
				"${CMAKE_BINARY_DIR}/browser-runtime-tests"
			--version "${MH_PACKAGE_VERSION}"
				${browser_runtime_options} --testing
		DEPENDS ${browser_runtime_dependencies} mh-browser-single-player-check
		VERBATIM)

	add_executable(mh-webgl-tron-check
		tests/browser/tron.c
		tests/support/tron_render.c)
	target_link_libraries(mh-webgl-tron-check PRIVATE mh_renderer_webgl)
	target_link_options(mh-webgl-tron-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createTronCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_FUNCTIONS=['_main','_mh_webgl_tron_check']"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-tron-check)

	add_executable(mh-webgl-renderer-check
		tests/browser/renderer.c
		tests/browser/retro.c
		tests/browser/renderer_depth.c)
	target_link_libraries(mh-webgl-renderer-check PRIVATE mh_renderer_webgl)
	string(CONCAT browser_exported_functions
		"-sEXPORTED_FUNCTIONS=['_main',"
		"'_mh_webgl_renderer_check',"
		"'_mh_webgl_renderer_recovery']")
	target_link_options(mh-webgl-renderer-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createRendererCheck
		-sINVOKE_RUN=0
		-sASYNCIFY=1
		"-sEXPORTED_RUNTIME_METHODS=['ccall']"
		"${browser_exported_functions}"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-renderer-check)

	add_executable(mh-webgl-ui-check tests/browser/ui.c)
	target_link_libraries(mh-webgl-ui-check PRIVATE
		mh_render_webgl_window
		mh_canvas_webgl
		mh_ui)
	target_link_options(mh-webgl-ui-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createUiCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_FUNCTIONS=['_main','_mh_webgl_ui_check']"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-ui-check)
endif()

if("webgl" IN_LIST shader_targets)
	add_executable(mh-webgl-textures-check tests/browser/textures.c)
	target_link_libraries(mh-webgl-textures-check PRIVATE mh_render_webgl)
	target_link_options(mh-webgl-textures-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createTextureCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_FUNCTIONS=['_main','_mh_webgl_texture_check']"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-webgl-textures-check)
endif()

if(TARGET mh_renderer_webgl)
	add_executable(mh-openmh-render-check tests/render/openmh_lighting.c)
	target_link_libraries(mh-openmh-render-check PRIVATE mh_renderer_webgl)
	target_link_options(mh-openmh-render-check PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createOpenMHCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_FUNCTIONS=['_main','_mh_openmh_render_check']"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2)
	mh_c89(mh-openmh-render-check)
endif()
