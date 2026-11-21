find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(fuzz_directory "${CMAKE_BINARY_DIR}/fuzz")
file(MAKE_DIRECTORY "${fuzz_directory}/artifacts")

add_custom_target(fuzz-seeds
	COMMAND "${Python3_EXECUTABLE}"
		"${CMAKE_SOURCE_DIR}/tests/fuzz/seeds.py"
		"${fuzz_directory}/corpus"
	VERBATIM)

set(fuzz_parsers ass network tga settings)

if(TARGET mh_webrtc_wire)
	list(APPEND fuzz_parsers webrtc sctp signaling)
endif()

foreach(parser IN LISTS fuzz_parsers)
	add_executable(fuzz-${parser} tests/fuzz/${parser}.c)
	set_target_properties(fuzz-${parser} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${fuzz_directory}")
	mh_c89(fuzz-${parser})
	target_link_options(fuzz-${parser} PRIVATE -fsanitize=fuzzer)
	add_dependencies(fuzz-${parser} fuzz-seeds)

	if(BUILD_TESTING)
		add_test(NAME fuzz-${parser}-smoke COMMAND fuzz-${parser}
			"${fuzz_directory}/corpus/${parser}" -runs=1000 -seed=1
			-max_len=65536 -timeout=10
			"-artifact_prefix=${fuzz_directory}/artifacts/")
		set_tests_properties(fuzz-${parser}-smoke PROPERTIES
			LABELS "fuzz" TIMEOUT 60)
	endif()
endforeach()
target_link_libraries(fuzz-ass PRIVATE mh_resource_zip mh_resource_memory)
target_link_libraries(fuzz-network PRIVATE mh_net_protocol mh_udp_framing)
target_link_libraries(fuzz-tga PRIVATE mh_assets)
target_link_libraries(fuzz-settings PRIVATE mh_settings)

if(TARGET fuzz-webrtc)
	target_link_libraries(fuzz-webrtc PRIVATE mh_webrtc_wire)
	target_link_libraries(fuzz-sctp PRIVATE mh_webrtc_wire)
	target_link_libraries(fuzz-signaling PRIVATE mh_webrtc_signaling)
endif()
