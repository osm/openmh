# Compose the interactive host independently of diagnostic launchers.
mh_dependency_version(sdl mh_sdl_version)
find_package(SDL3 ${mh_sdl_version} EXACT CONFIG REQUIRED)

add_library(mh_window_sdl OBJECT
	src/backends/window/sdl.c
	src/backends/window/sdl_display.c)
target_include_directories(mh_window_sdl PUBLIC src)
target_include_directories(mh_window_sdl SYSTEM PRIVATE
	"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(mh_window_sdl PUBLIC mh_window_system PRIVATE SDL3::SDL3)
mh_c89(mh_window_sdl)

# SDL's process startup is separate from window acquisition and rendering.
add_library(mh_window_sdl_startup STATIC src/backends/window/sdl_startup.c)
target_include_directories(mh_window_sdl_startup PUBLIC src)
target_include_directories(mh_window_sdl_startup SYSTEM PRIVATE
	"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(mh_window_sdl_startup PRIVATE SDL3::SDL3)
mh_c89(mh_window_sdl_startup)

if(EMSCRIPTEN)
	target_link_options(mh_window_sdl INTERFACE
		"SHELL:--js-library '${CMAKE_CURRENT_SOURCE_DIR}/src/backends/window/sdl_browser.js'")
	set_property(TARGET mh_window_sdl APPEND PROPERTY INTERFACE_LINK_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/backends/window/sdl_browser.js")
endif()

add_library(mh_input_sdl OBJECT
	src/backends/input/sdl_clipboard.c
	src/backends/input/sdl_input.c
	src/backends/input/sdl_keyboard.c
	src/backends/input/sdl_devices.c)
target_include_directories(mh_input_sdl PUBLIC src)
target_include_directories(mh_input_sdl SYSTEM PRIVATE
	"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(mh_input_sdl PUBLIC mh_input PRIVATE mh_text SDL3::SDL3)
mh_c89(mh_input_sdl)

target_link_libraries(mh_input_sdl PRIVATE mh_input_build)

foreach(device mouse joystick)
	if(device IN_LIST MH_INPUT_DEVICES)
		target_sources(mh_input_sdl PRIVATE
			src/backends/input/sdl_${device}.c)
	endif()
endforeach()

if(MH_INPUT_FEEDBACK AND "joystick" IN_LIST MH_INPUT_DEVICES)
	target_sources(mh_input_sdl PRIVATE
		src/backends/input/sdl_feedback.c)
endif()

add_library(mh_audio_sdl STATIC src/backends/audio/sdl_device.c)

if(EMSCRIPTEN)
	target_sources(mh_audio_sdl PRIVATE
		src/backends/audio/sdl_emscripten_worker.c)
else()
	target_sources(mh_audio_sdl PRIVATE
		src/backends/audio/sdl_worker.c)
endif()
target_include_directories(mh_audio_sdl PUBLIC src)
target_include_directories(mh_audio_sdl SYSTEM PRIVATE
	"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(mh_audio_sdl PUBLIC mh_audio_playback PRIVATE SDL3::SDL3)
mh_c89(mh_audio_sdl)

add_library(mh_canvas_sdl STATIC
	src/backends/render/sdl_canvas.c
	src/backends/render/sdl_canvas_image.c)
target_include_directories(mh_canvas_sdl PUBLIC src)
target_include_directories(mh_canvas_sdl SYSTEM PRIVATE
	"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(mh_canvas_sdl
	PUBLIC
		mh_canvas
	PRIVATE
		mh_window_sdl
		SDL3::SDL3)
mh_c89(mh_canvas_sdl)

if(EMSCRIPTEN)
	add_library(mh_audio_openal STATIC
		src/backends/audio/openal_provider.c
		src/backends/audio/openal_device.c
		src/backends/audio/openal_queue.c)
	target_include_directories(mh_audio_openal PUBLIC src)
	target_link_libraries(mh_audio_openal
		PUBLIC
			mh_audio_playback
		PRIVATE
			openal)
	set(openal_activation
		"${CMAKE_CURRENT_SOURCE_DIR}/src/backends/audio/openal_activation.js")
	target_link_options(mh_audio_openal INTERFACE
		"SHELL:--js-library '${openal_activation}'")
	set_property(TARGET mh_audio_openal APPEND PROPERTY
		INTERFACE_LINK_DEPENDS "${openal_activation}")
	mh_c89(mh_audio_openal)
else()
	# SDL rendering family. The leaf implementations remain independently linkable.
	if(MH_RENDER_GPU OR MH_BUILD_TOOLS OR BUILD_TESTING)
		include(cmake/sdl_gpu.cmake)
	endif()

	add_library(mh_render_sdl STATIC src/backends/render/sdl_service.c)
	target_link_libraries(mh_render_sdl
		PUBLIC
			mh_renderer
			mh_canvas
			mh_image_capture
		PRIVATE
			mh_canvas_sdl
			mh_window_sdl)
	mh_c89(mh_render_sdl)

	target_link_libraries(mh_render_sdl PRIVATE mh_render_build)

	if("software" IN_LIST MH_RENDER_PROFILES)
		target_link_libraries(mh_render_sdl PRIVATE mh_render_software)
	endif()

	if(MH_RENDER_GPU)
		target_link_libraries(mh_render_sdl PRIVATE mh_render_sdl_gpu)
	endif()
endif()

target_compile_definitions(mh_host_capabilities PRIVATE MH_HOST_CLIPBOARD=1)

if(TARGET mh_data_directory_filesystem AND NOT EMSCRIPTEN)
	if(MH_SCREENSHOT_FORMATS)
		target_compile_definitions(mh_host_capabilities PRIVATE
			MH_HOST_SCREENSHOTS=1)
	endif()
endif()

add_library(mh_host_sdl_clipboard STATIC src/client/hosts/sdl_clipboard.c)
target_link_libraries(mh_host_sdl_clipboard
	PUBLIC
		mh_input
	PRIVATE
		mh_input_sdl
		mh_host_capabilities)
mh_c89(mh_host_sdl_clipboard)

add_library(mh_host_common_services STATIC src/client/hosts/services.c)
target_link_libraries(mh_host_common_services
	PRIVATE
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
	PUBLIC
		mh_menus_app
	PRIVATE
		mh_host_screenshots
		mh_image_tga
		mh_image_fallback
		mh_image_pdi
		mh_font_fallback
		mh_sprite_spr
		mh_audio_fallback
		mh_host_clock
		mh_host_user_data
		mh_resource_zip
		mh_filesystem)
mh_c89(mh_host_common_services)

if(NOT EMSCRIPTEN)
	# The native entry point selects services; menus themselves remain portable.
	add_library(mh_host_sdl STATIC
		src/client/hosts/sdl_services.c
		src/client/hosts/sdl_application.c)
	target_link_libraries(mh_host_sdl PRIVATE mh_render_sdl)
	target_link_libraries(mh_host_sdl
		PUBLIC
			mh_host_common_services
		PRIVATE
			mh_input_sdl
			mh_audio_sdl
			mh_host_sdl_video
			mh_host_sdl_clipboard
			mh_host_capabilities
			mh_host_network)
	mh_c89(mh_host_sdl)
endif()
