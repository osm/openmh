add_library(mh_error STATIC src/engine/core/error.c)
target_include_directories(mh_error PUBLIC src)
mh_c89(mh_error)

add_library(mh_arguments STATIC src/engine/core/arguments.c)
target_include_directories(mh_arguments PUBLIC src)
mh_c89(mh_arguments)

add_library(mh_sha256 STATIC src/engine/core/sha256.c)
target_include_directories(mh_sha256 PUBLIC src)
mh_c89(mh_sha256)

add_library(mh_clock STATIC src/engine/core/clock.c)
target_include_directories(mh_clock PUBLIC src)
target_link_libraries(mh_clock PUBLIC mh_error)
mh_c89(mh_clock)

add_library(mh_text STATIC src/engine/core/text.c src/engine/core/charset.c)
target_link_libraries(mh_text PUBLIC mh_error)
target_include_directories(mh_text PUBLIC src)
mh_c89(mh_text)

add_library(mh_core STATIC
	src/engine/core/binary.c
	src/engine/core/pose.c
	src/engine/core/random.c
	src/engine/core/simulation_rate.c
	src/engine/core/rotation.c)
target_include_directories(mh_core PUBLIC src)
target_link_libraries(mh_core PUBLIC mh_clock mh_text)

if(UNIX)
	target_link_libraries(mh_core PUBLIC m)
endif()
mh_c89(mh_core)

add_library(mh_clock_source STATIC
	src/engine/clock/source.c
	src/engine/clock/paused.c)
target_include_directories(mh_clock_source PUBLIC src)
target_link_libraries(mh_clock_source PUBLIC mh_clock)
mh_c89(mh_clock_source)

add_library(mh_game_timer STATIC src/engine/clock/game_timer.c)
target_include_directories(mh_game_timer PUBLIC src)
target_link_libraries(mh_game_timer PUBLIC mh_clock_source mh_sim)
mh_c89(mh_game_timer)

# Calendar lookup needs only standard C; timing uses the selected OS adapter.
add_library(mh_calendar_system STATIC src/backends/clock/system_calendar.c)
target_link_libraries(mh_calendar_system PUBLIC mh_error)
mh_c89(mh_calendar_system)

if(WIN32 OR UNIX)
	add_library(mh_clock_system STATIC src/backends/clock/system_clock.c)

	if(WIN32)
		target_sources(mh_clock_system PRIVATE
			src/backends/clock/system_windows.c)
	else()
		target_sources(mh_clock_system PRIVATE
			src/backends/clock/system_posix.c)
	endif()
	target_link_libraries(mh_clock_system
		PUBLIC
			mh_clock_source
		PRIVATE
			mh_calendar_system)
	mh_c89(mh_clock_system)
endif()
