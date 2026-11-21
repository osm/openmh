include(cmake/shaders.cmake)

# Only the SDL consumer embeds these artifacts and knows SDL shader formats.
set(shader_headers)
set(shader_catalog "/* Generated SDL shader sources; do not edit. */\n")
set(shader_formats vulkan)

if("metal" IN_LIST shader_targets)
	list(APPEND shader_formats metal)
endif()

foreach(format IN LISTS shader_formats)
	set(extension spv)
	set(terminate OFF)

	if(format STREQUAL "metal")
		set(extension metal)
		set(terminate ON)
	endif()
	set(shader_sources
		"static const struct shader_source ${format}_sources[] = {\n")

	foreach(shader IN LISTS MH_SHADER_PROGRAMS)
		set(symbol "${format}_${shader}")
		set(artifact
			"${CMAKE_CURRENT_BINARY_DIR}/shaders/${format}/${shader}.${extension}")
		set(shader_header
			"${CMAKE_CURRENT_BINARY_DIR}/shaders/sdl/${symbol}.h")
		add_custom_command(OUTPUT "${shader_header}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory
				"${CMAKE_CURRENT_BINARY_DIR}/shaders/sdl"
			COMMAND "${CMAKE_COMMAND}" "-DINPUT=${artifact}"
				"-DOUTPUT=${shader_header}" "-DSYMBOL=${symbol}"
				"-DNULL_TERMINATE=${terminate}"
				-P
					"${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shader.cmake"
			DEPENDS "${artifact}" cmake/embed_shader.cmake
			VERBATIM)
		list(APPEND shader_headers "${shader_header}")
		string(APPEND shader_catalog "#include \"${symbol}.h\"\n")
		string(TOUPPER "${shader}" shader_id)
		string(REGEX REPLACE "_VERT$" "_VERTEX" shader_id
			"${shader_id}")
		string(REGEX REPLACE "_FRAG$" "_FRAGMENT" shader_id
			"${shader_id}")
		string(APPEND shader_sources
			"    {MH_GPU_${shader_id}, ${symbol}.bytes, sizeof(${symbol}.bytes)},\n")
	endforeach()
	string(APPEND shader_catalog "${shader_sources}};\n")
endforeach()
string(APPEND shader_catalog "static const struct shader_format formats[] = {\n"
	"    {SDL_GPU_SHADERFORMAT_SPIRV, "
	"\"main\", "
	"vulkan_sources, "
	"sizeof(vulkan_sources) / sizeof(vulkan_sources[0])},\n")

if("metal" IN_LIST shader_formats)
	string(APPEND shader_catalog
		"    {SDL_GPU_SHADERFORMAT_MSL, "
		"\"main0\", "
		"metal_sources, "
		"sizeof(metal_sources) / sizeof(metal_sources[0])},\n")
endif()
string(APPEND shader_catalog "};\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/sdl/shader_sources.h"
	CONTENT "${shader_catalog}")

add_library(mh_render_sdl_gpu STATIC
	src/backends/render/sdl_gpu_filter.c
	src/backends/render/sdl_gpu_glenz.c
	src/backends/render/sdl_gpu_device.c
	src/backends/render/sdl_gpu_shaders.c
	src/backends/render/sdl_gpu_vertex_input.c
	src/backends/render/sdl_gpu_backend.c
	src/backends/render/sdl_gpu_glide.c
	src/backends/render/sdl_gpu_direct3d.c
	src/backends/render/sdl_gpu_powervr.c
	src/backends/render/sdl_gpu_openmh.c
	src/backends/render/sdl_gpu_renderer.c
	src/backends/render/sdl_gpu_pipelines.c
	src/backends/render/sdl_gpu_pipeline_create.c
	src/backends/render/sdl_gpu_material_pipelines.c
	src/backends/render/sdl_gpu_mesh.c
	src/backends/render/sdl_gpu_scene.c
	src/backends/render/sdl_gpu_material_queue.c
	src/backends/render/sdl_gpu_material_draw.c
	src/backends/render/sdl_gpu_face_prepare.c
	src/backends/render/sdl_gpu_tron_scene.c
	src/backends/render/sdl_gpu_ui.c
	src/backends/render/sdl_gpu_frame.c
	src/backends/render/sdl_gpu_readback.c
	src/backends/render/sdl_gpu_tron.c
	src/backends/render/sdl_gpu_retro.c
	${shader_headers})
target_include_directories(mh_render_sdl_gpu PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}/shaders/sdl")
target_include_directories(mh_render_sdl_gpu SYSTEM PRIVATE
	"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(mh_render_sdl_gpu
	PUBLIC
		mh_renderer
		mh_render_common
		mh_ui
	PRIVATE
		mh_scene_materials
		mh_scene_tron
		mh_shader_layouts
		mh_window_sdl
		SDL3::SDL3)
mh_c89(mh_render_sdl_gpu)

target_link_libraries(mh_render_sdl_gpu PRIVATE mh_render_build)

if("openmh" IN_LIST MH_RENDER_PROFILES)
	target_sources(mh_render_sdl_gpu PRIVATE
		src/backends/render/sdl_gpu_openmh_pipelines.c)
endif()

if("direct3d" IN_LIST MH_RENDER_PROFILES)
	target_sources(mh_render_sdl_gpu PRIVATE
		src/backends/render/sdl_gpu_direct3d_pipelines.c)
endif()

if("powervr" IN_LIST MH_RENDER_PROFILES)
	target_sources(mh_render_sdl_gpu PRIVATE
		src/backends/render/sdl_gpu_powervr_pipelines.c
		src/backends/render/sdl_gpu_powervr_order.c
		src/backends/render/sdl_gpu_powervr_texture.c)
endif()
