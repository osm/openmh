# Stable public IDs are independent of the selected build subset and its order.
function(mh_profile_selection variable defaults)
	set(${variable} "${defaults}" CACHE STRING
		"Included profiles (${defaults})")

	if("${${variable}}" STREQUAL "" AND NOT ARGV2 STREQUAL "OPTIONAL")
		message(FATAL_ERROR
			"${variable} must select at least one profile")
	endif()

	foreach(profile IN LISTS ${variable})
		if(NOT profile IN_LIST defaults)
			message(FATAL_ERROR
				"Unknown ${variable} entry '${profile}'; choose from ${defaults}")
		endif()
	endforeach()
	list(REMOVE_DUPLICATES ${variable})
	set(${variable} "${${variable}}" PARENT_SCOPE)
	message(STATUS "${variable}: ${${variable}}")
endfunction()

mh_profile_selection(MH_RENDER_PROFILES
	"software;glide;direct3d;powervr;openmh")
mh_profile_selection(MH_AUDIO_PROFILES "software;ds3d;eax;a3d12;a3d20")
mh_profile_selection(MH_NETWORK_PROTOCOLS "legacy;openmh")

# GPU profiles and native menus share unlit material processing.
# Excluding a race profile does not remove this common presentation path.

set(profile_definitions)

foreach(profile software glide direct3d powervr openmh)
	string(TOUPPER "${profile}" upper)

	if(profile IN_LIST MH_RENDER_PROFILES)
		list(APPEND profile_definitions MH_RENDER_BUILD_${upper}=1)
	else()
		list(APPEND profile_definitions MH_RENDER_BUILD_${upper}=0)
	endif()
endforeach()
set(MH_RENDER_GPU OFF)

foreach(profile glide direct3d powervr openmh)
	if(profile IN_LIST MH_RENDER_PROFILES)
		set(MH_RENDER_GPU ON)
	endif()
endforeach()

if(MH_RENDER_GPU)
	list(APPEND profile_definitions MH_RENDER_BUILD_GPU=1)
else()
	list(APPEND profile_definitions MH_RENDER_BUILD_GPU=0)
endif()

add_library(mh_render_build INTERFACE)
target_compile_definitions(mh_render_build INTERFACE ${profile_definitions})

# Keyboard navigation/text entry is required by the current client UI.
mh_profile_selection(MH_INPUT_DEVICES "keyboard;mouse;joystick")

if(MH_BUILD_CLIENT AND NOT "keyboard" IN_LIST MH_INPUT_DEVICES)
	message(FATAL_ERROR
		"The current client menus require keyboard in MH_INPUT_DEVICES")
endif()
option(MH_INPUT_FEEDBACK "Include joystick force feedback" ON)
set(input_definitions)

foreach(device keyboard mouse joystick)
	string(TOUPPER "${device}" upper)

	if(device IN_LIST MH_INPUT_DEVICES)
		list(APPEND input_definitions MH_INPUT_BUILD_${upper}=1)
	else()
		list(APPEND input_definitions MH_INPUT_BUILD_${upper}=0)
	endif()
endforeach()

if(MH_INPUT_FEEDBACK AND "joystick" IN_LIST MH_INPUT_DEVICES)
	list(APPEND input_definitions MH_INPUT_BUILD_FEEDBACK=1)
else()
	list(APPEND input_definitions MH_INPUT_BUILD_FEEDBACK=0)
endif()

add_library(mh_input_build INTERFACE)
target_compile_definitions(mh_input_build INTERFACE ${input_definitions})

mh_profile_selection(MH_SCREENSHOT_FORMATS "png;tga" OPTIONAL)

add_library(mh_screenshot_build INTERFACE)

foreach(format png tga)
	string(TOUPPER "${format}" upper)

	if(format IN_LIST MH_SCREENSHOT_FORMATS)
		target_compile_definitions(mh_screenshot_build INTERFACE
			MH_SCREENSHOT_BUILD_${upper}=1)
	else()
		target_compile_definitions(mh_screenshot_build INTERFACE
			MH_SCREENSHOT_BUILD_${upper}=0)
	endif()
endforeach()

option(MH_USE_FFMPEG "Enable optional FFmpeg media support when available" ON)
option(MH_DECODE_OPUS "Include the FFmpeg Opus audio decoder" ON)
option(MH_DECODE_MOVIES "Include the FFmpeg movie decoder" ON)
option(MH_ENCODE_MEDIA "Include compact media encoding in the extractor" ON)
mh_profile_selection(MH_IMPORT_FORMATS "installshield;mscab" OPTIONAL)

list(LENGTH MH_AUDIO_PROFILES audio_profile_count)
list(LENGTH MH_SCREENSHOT_FORMATS screenshot_format_count)
set(MH_ALL_AUDIO_PROFILES OFF)
set(MH_ALL_SCREENSHOT_FORMATS OFF)

if(audio_profile_count EQUAL 5)
	set(MH_ALL_AUDIO_PROFILES ON)
endif()

if(screenshot_format_count EQUAL 2)
	set(MH_ALL_SCREENSHOT_FORMATS ON)
endif()
