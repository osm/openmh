# WebGL owns API state; visual profile policy stays in the portable renderer.
add_library(mh_render_webgl STATIC src/backends/render/webgl_pipeline.c)
target_link_libraries(mh_render_webgl PUBLIC mh_render_common)
mh_c89(mh_render_webgl)

if(NOT "webgl" IN_LIST shader_targets)
	return()
endif()

# The browser consumes the same stages as SDL, translated to GLSL ES offline.
set(webgl_headers)
set(webgl_catalog "/* Generated WebGL shader sources; do not edit. */\n")
set(webgl_sources "static const struct shader_source sources[] = {\n")

foreach(shader IN LISTS MH_SHADER_PROGRAMS)
	string(REGEX REPLACE "^.*_" "" stage "${shader}")
	set(artifact
		"${CMAKE_CURRENT_BINARY_DIR}/shaders/webgl/${shader}.${stage}")
	set(header
		"${CMAKE_CURRENT_BINARY_DIR}/shaders/webgl/embedded/${shader}.h")
	add_custom_command(OUTPUT "${header}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory
			"${CMAKE_CURRENT_BINARY_DIR}/shaders/webgl/embedded"
		COMMAND "${CMAKE_COMMAND}" "-DINPUT=${artifact}"
			"-DOUTPUT=${header}"
			"-DSYMBOL=webgl_${shader}" -DNULL_TERMINATE=ON
			-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shader.cmake"
		DEPENDS "${artifact}" cmake/embed_shader.cmake
		VERBATIM)
	list(APPEND webgl_headers "${header}")
	string(APPEND webgl_catalog "#include \"${shader}.h\"\n")
	string(TOUPPER "${shader}" shader_id)
	string(REGEX REPLACE "_VERT$" "_VERTEX" shader_id "${shader_id}")
	string(REGEX REPLACE "_FRAG$" "_FRAGMENT" shader_id "${shader_id}")
	string(APPEND webgl_sources
		"    {MH_GPU_${shader_id}, webgl_${shader}.bytes},\n")
endforeach()
string(APPEND webgl_catalog "${webgl_sources}};\n")
file(GENERATE OUTPUT
	"${CMAKE_CURRENT_BINARY_DIR}/shaders/webgl/embedded/shader_sources.h"
	CONTENT "${webgl_catalog}")
target_sources(mh_render_webgl PRIVATE
	src/backends/render/webgl_error.c
	src/backends/render/webgl_geometry.c
	src/backends/render/webgl_images.c
	src/backends/render/webgl_mesh.c
	src/backends/render/webgl_mesh_upload.c
	src/backends/render/webgl_scene.c
	src/backends/render/webgl_material_queue.c
	src/backends/render/webgl_material_storage.c
	src/backends/render/webgl_material_faces.c
	src/backends/render/webgl_material_draw.c
	src/backends/render/webgl_draw.c
	src/backends/render/webgl_frame.c
	src/backends/render/webgl_effect_draw.c
	src/backends/render/webgl_glenz.c
	src/backends/render/webgl_texture.c
	src/backends/render/webgl_sampler.c
	src/backends/render/webgl_target.c
	src/backends/render/webgl_presentation.c
	src/backends/render/webgl_readback.c
	src/backends/render/webgl_depth.c
	src/backends/render/webgl_history.c
	src/backends/render/webgl_ui.c
	src/backends/render/webgl_program.c
	src/backends/render/webgl_vertex_array.c
	${webgl_headers})
if("powervr" IN_LIST MH_RENDER_PROFILES)
	target_sources(mh_render_webgl PRIVATE src/backends/render/webgl_powervr_order.c)
endif()
target_link_options(mh_render_webgl INTERFACE
	"SHELL:--js-library '${CMAKE_CURRENT_SOURCE_DIR}/src/backends/render/webgl_readback.js'")
set_property(TARGET mh_render_webgl APPEND PROPERTY INTERFACE_LINK_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/backends/render/webgl_readback.js")
target_link_libraries(mh_render_webgl PRIVATE
	mh_shader_layouts
	mh_assets
	mh_scene_materials)
target_include_directories(mh_render_webgl PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}/shaders/webgl/embedded")

if(TARGET mh_window_sdl)
	add_library(mh_render_webgl_window STATIC
		src/backends/render/webgl_sdl_context.c)
	target_link_options(mh_render_webgl_window INTERFACE
		"SHELL:--js-library '${CMAKE_CURRENT_SOURCE_DIR}/src/backends/render/webgl_context.js'")
	set_property(TARGET mh_render_webgl_window APPEND PROPERTY
		INTERFACE_LINK_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/backends/render/webgl_context.js")
	target_link_libraries(mh_render_webgl_window
		PUBLIC
			mh_render_webgl
			mh_window_sdl
		PRIVATE
			SDL3::SDL3)
	target_include_directories(mh_render_webgl_window SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh_render_webgl_window)

	add_library(mh_canvas_webgl STATIC src/backends/render/webgl_canvas.c)
	target_link_libraries(mh_canvas_webgl PUBLIC
		mh_canvas
		mh_render_webgl_window)
	mh_c89(mh_canvas_webgl)

	add_library(mh_renderer_webgl STATIC
		src/backends/render/webgl_backend.c
		src/backends/render/webgl_glide.c
		src/backends/render/webgl_direct3d.c
		src/backends/render/webgl_powervr.c
		src/backends/render/webgl_openmh.c
		src/backends/render/webgl_renderer.c
		src/backends/render/webgl_renderer_recovery.c
		src/backends/render/webgl_renderer_frame.c
		src/backends/render/webgl_retro.c
		src/backends/render/webgl_tron.c
		src/backends/render/webgl_tron_scene.c
		src/backends/render/webgl_sdl_capture.c)
	target_link_libraries(mh_renderer_webgl PUBLIC
		mh_renderer
		mh_render_webgl_window
		mh_ui)
	target_link_libraries(mh_renderer_webgl PRIVATE mh_scene_tron)
	target_include_directories(mh_renderer_webgl SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh_renderer_webgl)
endif()

target_link_libraries(mh_render_webgl PRIVATE mh_render_build)

if(TARGET mh_renderer_webgl)
	target_link_libraries(mh_renderer_webgl PRIVATE mh_render_build)
endif()
