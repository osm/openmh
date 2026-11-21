# One release version for applications, archives and the browser runtime.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MH_PACKAGE_VERSION)
string(STRIP "${MH_PACKAGE_VERSION}" MH_PACKAGE_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

set(mh_dependency_manifest "${CMAKE_CURRENT_SOURCE_DIR}/docker/sources.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	"${mh_dependency_manifest}")

function(mh_dependency_version name output)
	file(STRINGS "${mh_dependency_manifest}" row REGEX "^${name} ")
	if(NOT row MATCHES "^${name} ([0-9.]+) ")
		message(FATAL_ERROR "Missing version for ${name} in ${mh_dependency_manifest}")
	endif()
	set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

add_library(mh_build_version INTERFACE)
target_compile_definitions(mh_build_version INTERFACE
	MH_BUILD_VERSION="${MH_PACKAGE_VERSION}")

# Player builds contain applications; development programs are opt-in.
option(MH_BUILD_CLIENT "Build the SDL3 game client" ON)
option(MH_DEBUG "Enable development command-line controls in openmotor" OFF)
option(MH_BUILD_TOOLS "Build development viewers and diagnostic launchers" OFF)
option(BUILD_TESTING "Build automated tests and acceptance fixtures" OFF)
option(MH_ENABLE_SANITIZERS "Check native code with ASan and UBSan" OFF)
option(MH_BUILD_FUZZERS "Build Clang libFuzzer parser checks" OFF)
option(MH_ENABLE_WEBRTC "Build WebRTC networking components" ON)

if(MH_ENABLE_SANITIZERS AND (CMAKE_CROSSCOMPILING OR
	NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$"))
	message(FATAL_ERROR
		"MH_ENABLE_SANITIZERS requires a native GCC or Clang build")
endif()

if(MH_BUILD_FUZZERS AND (NOT MH_ENABLE_SANITIZERS OR
	NOT CMAKE_C_COMPILER_ID STREQUAL "Clang"))
	message(FATAL_ERROR
		"MH_BUILD_FUZZERS requires Clang and MH_ENABLE_SANITIZERS=ON")
endif()

if(EMSCRIPTEN)
	include(cmake/wasm.cmake)
else()
	option(MH_BUILD_NETWORK "Build native LAN/INET client networking" ON)
	option(MH_BUILD_SERVER "Build the headless dedicated server" ON)
	option(MH_BUILD_EXTRACT "Build the standalone asset extractor" ON)
endif()
include(CTest)
include(cmake/profiles.cmake)
find_package(Python3 COMPONENTS Interpreter QUIET)

function(mh_development_targets path)
	if(MH_BUILD_TOOLS)
		set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
		include(${path})
	endif()
endfunction()

function(mh_test_targets path)
	if(BUILD_TESTING)
		set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
		include(${path})
	endif()
endfunction()

if(Python3_Interpreter_FOUND)
	add_custom_target(format
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/format.py"
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
		USES_TERMINAL)

	add_custom_target(format-check
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/format.py" --check
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_format.py"
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
		USES_TERMINAL)
endif()

# Packet and file lengths must never wrap. Some protocols also have counters
# that wrap by design, so mixed targets can select their framing sources.
function(mh_check_unsigned_lengths target)
	if(MH_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang")
		if(ARGN)
			set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS
				-fsanitize=unsigned-integer-overflow)
		else()
			target_compile_options(${target} PRIVATE
				-fsanitize=unsigned-integer-overflow)
		endif()
	endif()
endfunction()

function(mh_c89 target)
	if(MH_ENABLE_SANITIZERS)
		target_compile_options(${target} PRIVATE
			-fsanitize=address,undefined
			-fno-sanitize-recover=all
			-fno-omit-frame-pointer)
		target_link_options(${target} PUBLIC
			-fsanitize=address,undefined)
	endif()

	if(MH_BUILD_FUZZERS)
		# Instrument parser libraries as well as the fuzz entry points.
		target_compile_options(${target} PRIVATE
			-fsanitize=fuzzer-no-link)
	endif()
	set_target_properties(${target} PROPERTIES
		C_STANDARD 90
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)

	if(MINGW)
		# The Windows CRT treats long double as double. MinGW's ISO stdio
		# preserves our C89 %L formatting and scanning without changing math.
		target_compile_definitions(${target} PRIVATE
			__USE_MINGW_ANSI_STDIO=1)

		# Use the MinGW declarations' format checks instead of GCC's builtin
		# Windows-CRT signatures, which disagree about long double.
		foreach(stdio_function printf fprintf sprintf vprintf vfprintf
			vsprintf scanf fscanf sscanf)
			target_compile_options(${target} PRIVATE
				-fno-builtin-${stdio_function})
		endforeach()
	endif()

	if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(${target} PRIVATE
			-Wall
			-Wextra
			-Werror
			-pedantic-errors
			-Wdeclaration-after-statement
			-Wstrict-prototypes)
	elseif(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX)
	endif()
endfunction()
