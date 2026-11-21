add_library(mh_server_console_output STATIC src/server/console/output.c)
target_include_directories(mh_server_console_output PUBLIC src)
mh_c89(mh_server_console_output)

add_library(mh_server_config STATIC src/server/config/config.c)
target_link_libraries(mh_server_config PUBLIC mh_core mh_net_protocol)
mh_c89(mh_server_config)

add_library(mh_server_session STATIC
	src/server/session/admission.c
	src/server/logging/race.c
	src/server/logging/events.c
	src/server/session/bans.c
	src/server/session/kick_votes.c
	src/server/session/host_moderation.c
	src/server/commands/kick.c
	src/server/session/lobby.c
	src/server/session/host.c
	src/server/session/host_delivery.c
	src/server/session/host_race.c
	src/server/session/host_replication.c
	src/server/session/host_operator.c
	src/server/session/host_scores.c
	src/server/session/heartbeat.c
	src/server/session/ready.c
	src/server/session/score_clear.c
	src/server/session/grid.c
	src/server/session/setup.c
	src/server/session/return.c
	src/server/session/results.c
	src/server/session/standings.c
	src/server/session/progress.c
	src/server/session/clock.c
	src/server/session/vehicle.c
	src/server/commands/client.c
	src/server/commands/messages.c
	src/server/commands/information.c
	src/server/commands/laps.c
	src/server/commands/settings.c
	src/server/commands/operator.c)
target_link_libraries(mh_server_session PUBLIC
	mh_server_config
	mh_net_protocol
	mh_transport_dispatch
	mh_net_start
	mh_net_score
	mh_net_elimination
	mh_net_history
	mh_net_snapshot_schedule
	mh_standings_order
	mh_route_geometry)
target_link_libraries(mh_server_session PRIVATE mh_build_version mh_lap_presets)
mh_c89(mh_server_session)

add_library(mh_server_courses STATIC
	src/server/courses/catalog.c
	src/server/courses/courses.c)
target_link_libraries(mh_server_courses PUBLIC
	mh_server_config
	mh_navigation_loading
	PRIVATE mh_catalog_loading mh_horn_loading)
mh_c89(mh_server_courses)

add_library(mh_server_config_load STATIC src/server/config/load.c)
target_link_libraries(mh_server_config_load PUBLIC
	mh_server_config
	mh_resource_source)
mh_c89(mh_server_config_load)

if(MH_BUILD_SERVER AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32))
	add_executable(openmotorsv
		src/server/cli/arguments.c
		src/server/cli/main.c
		src/server/console/console.c
		src/server/logging/file.c)
	target_link_libraries(openmotorsv PRIVATE
		mh_build_version
		mh_server_console_output
		mh_arguments
		mh_server_session
		mh_server_config_load
		mh_server_courses
		mh_catalog_fallback
		mh_horn_fallback
		mh_navigation_mot
		mh_transport_udp
		mh_udp
		mh_clock_system
		mh_resource_filesystem
		mh_resource_zip
		mh_content)

	if(MH_ENABLE_WEBRTC AND "openmh" IN_LIST MH_NETWORK_PROTOCOLS)
		target_sources(openmotorsv PRIVATE src/server/cli/browser.c)
		target_compile_definitions(openmotorsv PRIVATE
			MH_ENABLE_WEBRTC=1)
		target_link_libraries(openmotorsv PRIVATE
			mh_transport_webrtc
			mh_transport_combined
			mh_signaling_http)
	endif()

	if(WIN32)
		target_sources(openmotorsv PRIVATE
			src/server/hosts/windows_main.c
			src/server/hosts/windows_console.c
			src/server/hosts/windows_log.c)
		target_link_libraries(openmotorsv PRIVATE
			mh_resolver_windows
			mh_windows_text)
		target_link_options(openmotorsv PRIVATE -municode)
	else()
		target_sources(openmotorsv PRIVATE
			src/server/hosts/posix_main.c
			src/server/hosts/posix_console.c
			src/server/hosts/posix_log.c)
		target_link_libraries(openmotorsv PRIVATE mh_resolver_posix)
	endif()
	mh_c89(openmotorsv)
endif()
