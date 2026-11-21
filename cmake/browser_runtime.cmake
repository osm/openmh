add_library(mh_runtime_emscripten STATIC src/backends/runtime/emscripten_loop.c)
target_include_directories(mh_runtime_emscripten PUBLIC src)
mh_c89(mh_runtime_emscripten)
set(browser_loop_bridge
	"${CMAKE_CURRENT_SOURCE_DIR}/src/backends/runtime/emscripten_loop.js")
target_link_options(mh_runtime_emscripten INTERFACE
	"SHELL:--js-library '${browser_loop_bridge}'")
set_property(TARGET mh_runtime_emscripten APPEND PROPERTY
	INTERFACE_LINK_DEPENDS "${browser_loop_bridge}")

function(mh_browser_application target)
	add_executable(${target}
		src/client/hosts/browser_main.c
		src/client/hosts/browser_render.c
		src/client/hosts/browser_services.c
		src/client/hosts/browser_race_api.c
		src/client/hosts/browser_multiplayer_api.c
		src/client/hosts/browser_preferences_api.c
		src/backends/storage/browser_package_check.c
		src/backends/storage/indexeddb_storage.c)
	target_link_libraries(${target} PRIVATE
		mh_render_build
		mh_host_common_services
		mh_host_sdl_clipboard
		mh_canvas_webgl
		mh_window_sdl
		mh_video_playback
		mh_clock_source
		mh_input_sdl
		mh_audio_openal
		mh_runtime_emscripten)

	if(MH_RENDER_GPU)
		target_link_libraries(${target} PRIVATE mh_renderer_webgl)
	endif()

	if("software" IN_LIST MH_RENDER_PROFILES)
		target_link_libraries(${target} PRIVATE mh_render_software)
	endif()

	if(MH_HAVE_FFMPEG AND MH_DECODE_MOVIES)
		target_compile_definitions(${target} PRIVATE MH_BROWSER_VIDEO=1)
		target_link_libraries(${target} PRIVATE
			mh_video_divi
			mh_video_ffmpeg)
	endif()

	if(MH_ENABLE_WEBRTC AND "openmh" IN_LIST MH_NETWORK_PROTOCOLS)
		target_compile_definitions(${target} PRIVATE MH_BROWSER_NETWORK=1)
		target_link_libraries(${target} PRIVATE mh_transport_browser)
	endif()
	mh_c89(${target})
	set_target_properties(${target} PROPERTIES SUFFIX ".js")
	set(browser_host "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hosts")
	set(browser_storage
		"${CMAKE_CURRENT_SOURCE_DIR}/src/backends/storage")
	string(CONCAT browser_exported_functions
		"-sEXPORTED_FUNCTIONS=['_main',"
		"'_mh_browser_preferences_error',"
		"'_mh_browser_preferences_count',"
		"'_mh_browser_preferences_text',"
		"'_mh_browser_preferences_number',"
		"'_mh_browser_preferences_begin',"
		"'_mh_browser_preferences_set',"
		"'_mh_browser_preferences_commit',"
		"'_mh_browser_controls_key',"
		"'_mh_browser_controls_get',"
		"'_mh_browser_controls_begin',"
		"'_mh_browser_controls_set',"
		"'_mh_browser_controls_commit',"
		"'_mh_browser_retry_storage',"
		"'_mh_browser_set_suspended',"
		"'_mh_browser_checkpoint',"
		"'_mh_browser_save_pause',"
		"'_mh_browser_close',"
		"'_mh_browser_race_enable',"
		"'_mh_browser_race_state',"
		"'_mh_browser_race_completed',"
		"'_mh_browser_race_count',"
		"'_mh_browser_race_choice',"
		"'_mh_browser_race_control_key',"
		"'_mh_browser_race_bind_key',"
		"'_mh_browser_race_available',"
		"'_mh_browser_race_reset',"
		"'_mh_browser_race_get',"
		"'_mh_browser_race_set',"
		"'_mh_browser_race_player',"
		"'_mh_browser_race_name',"
		"'_mh_browser_race_team',"
		"'_mh_browser_race_set_team',"
		"'_mh_browser_race_start',"
		"'_mh_browser_race_leave',"
		"'_mh_browser_race_error',"
		"'_mh_browser_race_progress',"
		"'_mh_browser_race_result',"
		"'_mh_browser_race_car_rating',"
		"'_mh_browser_race_track_detail',"
		"'_mh_browser_race_track_music',"
		"'_mh_browser_race_map_load',"
		"'_mh_browser_race_map_size',"
		"'_mh_browser_race_map_pixels',"
		"'_mh_browser_race_resolution_count',"
		"'_mh_browser_race_resolution',"
		"'_mh_browser_race_name_color',"
		"'_mh_browser_multiplayer_available',"
		"'_mh_browser_multiplayer_configure',"
		"'_mh_browser_multiplayer_ice',"
		"'_mh_browser_multiplayer_join',"
		"'_mh_browser_multiplayer_leave',"
		"'_mh_browser_multiplayer_return',"
		"'_mh_browser_multiplayer_player',"
		"'_mh_browser_multiplayer_show_fps',"
		"'_mh_browser_multiplayer_chat',"
		"'_mh_browser_multiplayer_ready',"
		"'_mh_browser_multiplayer_toggle_ready',"
		"'_mh_browser_multiplayer_error',"
		"'_mh_browser_multiplayer_refresh',"
		"'_mh_browser_multiplayer_number',"
		"'_mh_browser_multiplayer_text']")
	target_link_options(${target} PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createOpenMH
		-sINVOKE_RUN=0
		-lworkerfs.js
		"-sEXPORTED_RUNTIME_METHODS=['FS','MEMFS','WORKERFS','callMain','ccall','HEAPU8']"
		"${browser_exported_functions}"
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2
		"SHELL:--js-library '${browser_host}/browser_bridge.js'"
		"SHELL:--js-library '${browser_storage}/indexeddb_bridge.js'")
	set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
		"${browser_host}/browser_bridge.js"
		"${browser_storage}/indexeddb_bridge.js")
endfunction()

mh_browser_application(openmotor)
# Version the public browser runtime and its services as one package.
set(browser_host "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hosts")
set(browser_storage "${CMAKE_CURRENT_SOURCE_DIR}/src/backends/storage")

foreach(file storage.js database.js)
	configure_file("${browser_storage}/indexeddb_${file}"
		"${CMAKE_BINARY_DIR}/${file}" COPYONLY)
endforeach()
configure_file("${browser_storage}/indexeddb_packages.js"
	"${CMAKE_BINARY_DIR}/package-database.js" COPYONLY)
configure_file("${browser_storage}/browser_package_check.js"
	"${CMAKE_BINARY_DIR}/package-check.js" COPYONLY)
configure_file(src/extract/hosts/browser_job.js
	"${CMAKE_BINARY_DIR}/extract-job.js" COPYONLY)

foreach(file lifecycle.js checkpoints.js save-session.js backup.js
	race-history.js multiplayer.js preferences.js player.js session.js)
	string(REPLACE "-" "_" source_name "${file}")
	configure_file("${browser_host}/browser_${source_name}"
		"${CMAKE_BINARY_DIR}/${file}" COPYONLY)
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(browser_runtime_options)
set(browser_runtime_dependencies openmotor)

if(TARGET openmotorex)
	list(APPEND browser_runtime_options --extractor)
	list(APPEND browser_runtime_dependencies openmotorex)
endif()

set(MH_WASM_LICENSE_NOTICES "" CACHE FILEPATH
	"Combined OpenMH, Emscripten and dependency notices for this build")
set(browser_archive "${CMAKE_BINARY_DIR}/openmh-${MH_PACKAGE_VERSION}-wasm.tar.gz")
if(MH_WASM_LICENSE_NOTICES)
	list(APPEND browser_runtime_options --notices "${MH_WASM_LICENSE_NOTICES}")
	set(browser_archive_options --archive "${browser_archive}")
else()
	# Reconfiguring a release build for development invalidates its old archive.
	file(REMOVE "${browser_archive}")
endif()

add_custom_target(wasm-runtime
	COMMAND "${Python3_EXECUTABLE}"
		"${CMAKE_SOURCE_DIR}/tools/package_browser_runtime.py"
		--build "${CMAKE_BINARY_DIR}" --output
			"${CMAKE_BINARY_DIR}/browser-runtime"
		--version "${MH_PACKAGE_VERSION}" ${browser_runtime_options}
		${browser_archive_options}
	DEPENDS ${browser_runtime_dependencies}
	VERBATIM)
