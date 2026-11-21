# Development checks resolve target names without assuming output directories.
get_property(project_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(executable_entries)

foreach(target IN LISTS project_targets)
	get_target_property(target_type ${target} TYPE)

	if(target_type STREQUAL "EXECUTABLE")
		list(APPEND executable_entries
			"  \"${target}\": \"$<TARGET_FILE:${target}>\"")
	endif()
endforeach()
string(JOIN ",\n" executable_json ${executable_entries})
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/executables-$<CONFIG>.json"
	CONTENT "{\n${executable_json}\n}\n")
