# Runtime contract checks for the selected composition, including sparse IDs.
if(MH_BUILD_CLIENT AND NOT EMSCRIPTEN)
	add_executable(test-build-profiles tests/integration/build_profiles.c)
	target_link_libraries(test-build-profiles PRIVATE
		mh_application
		mh_host_common_services
		mh_render_sdl
		mh_input_sdl)
	mh_c89(test-build-profiles)
	set(render_mask 0)
	set(audio_mask 0)
	set(input_mask 0)
	set(screenshot_mask 0)
	set(index 0)

	foreach(profile software glide direct3d powervr openmh)
		if(profile IN_LIST MH_RENDER_PROFILES)
			math(EXPR render_mask
				"${render_mask} | (1 << ${index})")
		endif()
		math(EXPR index "${index} + 1")
	endforeach()
	set(index 0)

	foreach(profile software ds3d eax a3d12 a3d20)
		if(profile IN_LIST MH_AUDIO_PROFILES)
			math(EXPR audio_mask "${audio_mask} | (1 << ${index})")
		endif()
		math(EXPR index "${index} + 1")
	endforeach()
	set(index 0)

	foreach(device keyboard mouse joystick)
		if(device IN_LIST MH_INPUT_DEVICES)
			math(EXPR input_mask "${input_mask} | (1 << ${index})")
		endif()
		math(EXPR index "${index} + 1")
	endforeach()

	if(MH_INPUT_FEEDBACK AND "joystick" IN_LIST MH_INPUT_DEVICES)
		math(EXPR input_mask "${input_mask} | 8")
	endif()

	if("png" IN_LIST MH_SCREENSHOT_FORMATS)
		math(EXPR screenshot_mask "${screenshot_mask} | 1")
	endif()

	if("tga" IN_LIST MH_SCREENSHOT_FORMATS)
		math(EXPR screenshot_mask "${screenshot_mask} | 2")
	endif()
	add_test(NAME build-profile-selection COMMAND test-build-profiles
		${render_mask} ${audio_mask} ${input_mask} ${screenshot_mask})
endif()

if(TARGET openmotor AND NOT CMAKE_CROSSCOMPILING AND CMAKE_NM AND
	Python3_Interpreter_FOUND)
	set(binary_options)
	set(binary_protocols "")

	if(client_network_available)
		set(binary_protocols "${MH_NETWORK_PROTOCOLS}")
	endif()

	if(TARGET openmotorsv)
		list(APPEND binary_options --server "$<TARGET_FILE:openmotorsv>")

		if(NOT "openmh" IN_LIST MH_NETWORK_PROTOCOLS)
			list(APPEND binary_options --legacy-server)
		elseif(MH_ENABLE_WEBRTC)
			list(APPEND binary_options --webrtc)
		endif()
	endif()

	add_test(NAME build-profile-binary
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/profile_binary.py"
			"$<TARGET_FILE:openmotor>" "${CMAKE_NM}"
			"${MH_RENDER_PROFILES}" --protocols "${binary_protocols}"
			${binary_options})
endif()
