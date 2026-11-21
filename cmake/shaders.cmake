include_guard(GLOBAL)

# Explicit offline outputs are independent of the client. Native GPU clients,
# client tests and tools add Vulkan shaders. Browser clients add WebGL shaders.
set(MH_SHADER_TARGETS "" CACHE STRING
	"Additional shader outputs: vulkan;metal;webgl")
set(shader_targets ${MH_SHADER_TARGETS})

if(MH_BUILD_CLIENT AND NOT EMSCRIPTEN AND (MH_RENDER_GPU OR MH_BUILD_TOOLS OR
	BUILD_TESTING))
	list(APPEND shader_targets vulkan)
elseif(MH_BUILD_CLIENT AND EMSCRIPTEN)
	list(APPEND shader_targets webgl)
endif()
list(REMOVE_DUPLICATES shader_targets)

foreach(target IN LISTS shader_targets)
	if(NOT target MATCHES "^(vulkan|metal|webgl)$")
		message(FATAL_ERROR
			"Unknown shader target '${target}': use vulkan, metal or webgl")
	endif()
endforeach()

if(NOT shader_targets)
	return()
endif()

add_library(mh_shader_layouts STATIC
	src/backends/render/shader_vertex_layout.c
	src/backends/render/shader_material.c)
target_include_directories(mh_shader_layouts PUBLIC src)
mh_c89(mh_shader_layouts)

find_program(MH_GLSLANG glslangValidator)

if(NOT MH_GLSLANG)
	message(FATAL_ERROR
		"Selected shader outputs require glslangValidator (MH_GLSLANG)")
endif()

if("metal" IN_LIST shader_targets OR "webgl" IN_LIST shader_targets)
	find_program(MH_SPIRV_CROSS spirv-cross)

	if(NOT MH_SPIRV_CROSS)
		message(FATAL_ERROR
			"Metal/WebGL shader generation requires spirv-cross (MH_SPIRV_CROSS)")
	endif()
endif()

set(shader_root "${CMAKE_CURRENT_SOURCE_DIR}/src/backends/render")
set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(MH_SHADER_PROGRAMS)
string(REPLACE ";" "\n" shader_target_list "${shader_targets}")
file(GENERATE OUTPUT "${shader_output}/targets.txt"
	CONTENT "${shader_target_list}\n")

function(mh_shader_program name source stage)
	set(spirv "${shader_output}/vulkan/${name}.spv")
	add_custom_command(OUTPUT "${spirv}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory
			"${shader_output}/vulkan"
		COMMAND "${MH_GLSLANG}" -V ${ARGN}
			"${shader_root}/shader_${source}" -o "${spirv}"
		DEPENDS "${shader_root}/shader_${source}" ${shader_dependencies}
			"${CMAKE_CURRENT_SOURCE_DIR}/cmake/shaders.cmake"
				"${MH_GLSLANG}"
		VERBATIM)
	set(vulkan_outputs ${vulkan_outputs} "${spirv}" PARENT_SCOPE)

	if("metal" IN_LIST shader_targets)
		set(metal "${shader_output}/metal/${name}.metal")
		add_custom_command(OUTPUT "${metal}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory
				"${shader_output}/metal"
			COMMAND "${MH_SPIRV_CROSS}" "${spirv}" --msl
				--msl-version 20100
				--msl-decoration-binding --output "${metal}"
			DEPENDS "${spirv}"
				"${CMAKE_CURRENT_SOURCE_DIR}/cmake/shaders.cmake"
				"${MH_SPIRV_CROSS}"
			VERBATIM)
		set(metal_outputs ${metal_outputs} "${metal}" PARENT_SCOPE)
	endif()

	if("webgl" IN_LIST shader_targets)
		set(webgl "${shader_output}/webgl/${name}.${stage}")
		add_custom_command(OUTPUT "${webgl}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory
				"${shader_output}/webgl"
			COMMAND "${MH_SPIRV_CROSS}" "${spirv}" --es --version
				300
				--fixup-clipspace --output "${webgl}"
			DEPENDS "${spirv}"
				"${CMAKE_CURRENT_SOURCE_DIR}/cmake/shaders.cmake"
				"${MH_SPIRV_CROSS}"
			VERBATIM)
		set(webgl_outputs ${webgl_outputs} "${webgl}" PARENT_SCOPE)
	endif()
	set(MH_SHADER_PROGRAMS ${MH_SHADER_PROGRAMS} ${name} PARENT_SCOPE)
endfunction()

# Material stages serve unlit presentation and the Glide/OpenMH profiles.
foreach(family material direct3d powervr)
	if(NOT family STREQUAL "material" AND
		NOT family IN_LIST MH_RENDER_PROFILES)
		continue()
	endif()
	set(shader_dependencies)
	mh_shader_program(${family}_vert ${family}.vert vert)
	mh_shader_program(${family}_glenz_vert ${family}.vert vert
		-DMH_GLENZ=1)

	if(family STREQUAL "powervr")
		set(shader_dependencies "${shader_root}/shader_powervr.glsl")
	endif()
	mh_shader_program(${family}_frag ${family}.frag frag)
endforeach()
set(shader_dependencies)

foreach(shader ui tron retro depth)
	foreach(stage vert frag)
		mh_shader_program(${shader}_${stage} ${shader}.${stage}
			${stage})
	endforeach()
endforeach()

if("openmh" IN_LIST MH_RENDER_PROFILES)
	mh_shader_program(openmh_vert openmh.vert vert)
endif()

add_custom_target(mh_shaders ALL)

foreach(target IN LISTS shader_targets)
	add_custom_target(mh_shaders_${target} DEPENDS ${${target}_outputs})
	add_dependencies(mh_shaders mh_shaders_${target})
endforeach()
