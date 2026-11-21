add_library(mh_net_protocol STATIC
	src/engine/network/protocol_messages.c
	src/engine/network/protocol_profile.c
	src/engine/network/join.c
	src/engine/network/driver.c
	src/engine/network/vehicle_message.c
	src/engine/network/packet.c
	src/engine/network/vehicle_decode.c
	src/engine/network/vehicle_encode.c)
target_link_libraries(mh_net_protocol PUBLIC mh_core PRIVATE mh_sha256)
mh_c89(mh_net_protocol)
mh_check_unsigned_lengths(mh_net_protocol)

add_library(mh_network_build INTERFACE)

foreach(protocol legacy openmh)
	string(TOUPPER "${protocol}" upper)

	if(protocol IN_LIST MH_NETWORK_PROTOCOLS)
		target_sources(mh_net_protocol PRIVATE
			src/engine/network/${protocol}.c)
		target_compile_definitions(mh_network_build INTERFACE
			MH_NET_BUILD_${upper}=1)
	else()
		target_compile_definitions(mh_network_build INTERFACE
			MH_NET_BUILD_${upper}=0)
	endif()
endforeach()
target_link_libraries(mh_net_protocol PRIVATE mh_network_build)

add_library(mh_net_discovery STATIC src/engine/network/discovery.c)
target_link_libraries(mh_net_discovery PUBLIC mh_net_protocol)
mh_c89(mh_net_discovery)

add_library(mh_net_start STATIC src/engine/network/start.c)
target_link_libraries(mh_net_start PUBLIC mh_net_protocol)
mh_c89(mh_net_start)

add_library(mh_net_score STATIC src/engine/network/score.c)
target_link_libraries(mh_net_score PUBLIC mh_net_protocol)
mh_c89(mh_net_score)

add_library(mh_net_elimination STATIC src/engine/network/elimination.c)
target_link_libraries(mh_net_elimination PUBLIC mh_net_protocol)
mh_c89(mh_net_elimination)

add_library(mh_net_history STATIC src/engine/network/history.c)
target_link_libraries(mh_net_history PUBLIC mh_net_protocol)
mh_c89(mh_net_history)

add_library(mh_net_snapshot_schedule STATIC
	src/engine/network/snapshot_schedule.c)
target_link_libraries(mh_net_snapshot_schedule PUBLIC mh_net_protocol)
mh_c89(mh_net_snapshot_schedule)

add_library(mh_net STATIC
	src/engine/network/lobby.c
	src/engine/network/session.c
	src/engine/network/session_connection.c
	src/engine/network/session_timing.c
	src/engine/network/session_controls.c
	src/engine/network/session_text.c
	src/engine/network/session_receive.c
	src/engine/network/session_race.c
	src/engine/network/session_replication.c)
target_sources(mh_net PRIVATE src/engine/network/lobby_commands.c)
target_sources(mh_net PRIVATE src/engine/network/car_catalog.c)
target_sources(mh_net PRIVATE
	src/engine/network/replica.c
	src/engine/network/finish.c)
target_link_libraries(mh_net PUBLIC
	mh_net_protocol
	mh_net_discovery
	mh_net_start
	mh_net_score
	mh_net_elimination
	mh_net_history
	mh_net_snapshot_schedule
	mh_assets
	mh_input)
mh_c89(mh_net)

add_library(mh_reliable_delivery STATIC
	src/backends/network/reliable_delivery.c)
target_include_directories(mh_reliable_delivery PUBLIC src)
target_link_libraries(mh_reliable_delivery PUBLIC mh_core)
mh_c89(mh_reliable_delivery)

add_library(mh_udp_framing STATIC
	src/backends/network/udp_control.c
	src/backends/network/udp_shared.c)
target_include_directories(mh_udp_framing PUBLIC src)
target_link_libraries(mh_udp_framing PUBLIC mh_core)
mh_c89(mh_udp_framing)
mh_check_unsigned_lengths(mh_udp_framing)

add_library(mh_udp_description STATIC
	src/backends/network/udp_description.c)
target_link_libraries(mh_udp_description PUBLIC mh_net_discovery mh_udp_framing)
mh_c89(mh_udp_description)

add_library(mh_transport_dispatch STATIC
	src/engine/network/endpoint.c
	src/engine/network/transport.c
	src/engine/network/channel.c
	src/engine/network/transport_channel.c
	src/engine/network/transport_target.c)
target_include_directories(mh_transport_dispatch PUBLIC src)
target_link_libraries(mh_transport_dispatch PUBLIC mh_core PRIVATE mh_arguments)
mh_c89(mh_transport_dispatch)

add_library(mh_network_session STATIC
	src/engine/network/network_session.c
	src/engine/network/network_address.c)
target_link_libraries(mh_network_session PUBLIC mh_net mh_transport_dispatch)
mh_c89(mh_network_session)

add_library(mh_net_sim STATIC
	src/engine/network/drive.c
	src/engine/network/race.c
	src/engine/network/race_completion.c)
target_link_libraries(mh_net_sim PUBLIC mh_net mh_sim)
mh_c89(mh_net_sim)

add_library(mh_resolver STATIC src/engine/network/resolver.c)
target_include_directories(mh_resolver PUBLIC src)
target_link_libraries(mh_resolver PUBLIC mh_error)
mh_c89(mh_resolver)

if((UNIX OR WIN32) AND NOT EMSCRIPTEN)
	add_library(mh_udp STATIC
		src/backends/network/udp_address.c
		src/backends/network/udp_wait.c
		src/backends/network/udp_multiplex.c)

	if(WIN32)
		target_sources(mh_udp PRIVATE
			src/backends/network/udp_windows.c)
		target_link_libraries(mh_udp PRIVATE ws2_32)
	else()
		target_sources(mh_udp PRIVATE src/backends/network/udp_posix.c)
	endif()
	target_include_directories(mh_udp PUBLIC src)
	target_link_libraries(mh_udp PRIVATE mh_error)
	mh_c89(mh_udp)
endif()

set(native_webrtc OFF)
if(NOT EMSCRIPTEN AND (BUILD_TESTING OR
	(MH_BUILD_SERVER AND "openmh" IN_LIST MH_NETWORK_PROTOCOLS AND
	(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32))))
	set(native_webrtc ON)
endif()

if(MH_ENABLE_WEBRTC AND (native_webrtc OR BUILD_TESTING OR MH_BUILD_FUZZERS OR
	(EMSCRIPTEN AND MH_BUILD_CLIENT AND "openmh" IN_LIST MH_NETWORK_PROTOCOLS)))
	add_library(mh_webrtc_wire STATIC
		src/backends/network/webrtc_wire.c
		src/backends/network/webrtc_stun.c
		src/backends/network/webrtc_sdp.c
		src/backends/network/webrtc_sctp.c
		src/backends/network/webrtc_sctp_receive.c
		src/backends/network/webrtc_sctp_send.c
		src/backends/network/webrtc_sctp_packet.c)
	target_include_directories(mh_webrtc_wire PUBLIC src)
	target_link_libraries(mh_webrtc_wire PUBLIC mh_error PRIVATE mh_text)
	mh_c89(mh_webrtc_wire)
	mh_check_unsigned_lengths(mh_webrtc_wire
		src/backends/network/webrtc_sdp.c
		src/backends/network/webrtc_sctp_packet.c
		src/backends/network/webrtc_stun.c)

	add_library(mh_webrtc_signaling STATIC
		src/backends/network/webrtc_signaling_read.c
		src/backends/network/webrtc_signaling_write.c)
	target_include_directories(mh_webrtc_signaling PUBLIC src)
	target_link_libraries(mh_webrtc_signaling PRIVATE mh_text)
	mh_c89(mh_webrtc_signaling)
	mh_check_unsigned_lengths(mh_webrtc_signaling)

	if(EMSCRIPTEN)
		add_library(mh_transport_browser STATIC
			src/backends/network/browser_transport.c
			src/backends/network/browser_target.c)
		target_include_directories(mh_transport_browser PUBLIC src)
		target_link_libraries(mh_transport_browser
			PUBLIC
				mh_transport_dispatch
			PRIVATE
				mh_reliable_delivery
				mh_text)
		set(browser_network_bridge
			"${CMAKE_CURRENT_SOURCE_DIR}/src/backends/network/browser_bridge.js")
		target_link_options(mh_transport_browser INTERFACE
			"SHELL:--js-library '${browser_network_bridge}'")
		set_property(TARGET mh_transport_browser APPEND PROPERTY
			INTERFACE_LINK_DEPENDS "${browser_network_bridge}")
		mh_c89(mh_transport_browser)
	endif()

	if(native_webrtc)
		add_library(mh_transport_combined STATIC
			src/backends/network/combined_transport.c)
		target_link_libraries(mh_transport_combined PUBLIC
			mh_transport_dispatch)
		mh_c89(mh_transport_combined)

		# The native DTLS transport uses OpenSSL. Browsers supply their own TLS.
		find_package(OpenSSL 3 REQUIRED COMPONENTS SSL Crypto)

		add_library(mh_webrtc_crypto STATIC
			src/backends/network/webrtc_stun_auth.c
			src/backends/network/webrtc_dtls.c)
		target_include_directories(mh_webrtc_crypto PUBLIC src)
		target_link_libraries(mh_webrtc_crypto
			PUBLIC
				mh_webrtc_wire
			PRIVATE
				OpenSSL::Crypto
				OpenSSL::SSL
				mh_text)
		mh_c89(mh_webrtc_crypto)

		add_library(mh_transport_webrtc STATIC
			src/backends/network/webrtc_server.c
			src/backends/network/webrtc_offer.c
			src/backends/network/webrtc_peer.c)
		target_include_directories(mh_transport_webrtc PUBLIC src)
		target_link_libraries(mh_transport_webrtc
			PUBLIC
				mh_transport_dispatch
			PRIVATE
				mh_udp
				mh_webrtc_crypto
				mh_reliable_delivery
				mh_clock_source
				mh_text)
		mh_c89(mh_transport_webrtc)

		add_library(mh_signaling_http STATIC
			src/backends/network/signaling_server.c)
		target_include_directories(mh_signaling_http PUBLIC src)
		target_link_libraries(mh_signaling_http PRIVATE
			mh_webrtc_signaling
			mh_clock_source
			mh_text)

		if(WIN32)
			target_sources(mh_signaling_http PRIVATE
				src/backends/network/signaling_windows.c)
			target_link_libraries(mh_signaling_http PRIVATE ws2_32)
		else()
			target_sources(mh_signaling_http PRIVATE
				src/backends/network/signaling_posix.c)
		endif()
		mh_c89(mh_signaling_http)
	endif()
endif()

if(UNIX AND NOT EMSCRIPTEN)
	find_package(Threads REQUIRED)

	add_library(mh_resolver_posix STATIC
		src/backends/network/posix_resolver.c)
	target_link_libraries(mh_resolver_posix
		PUBLIC
			mh_resolver
		PRIVATE
			Threads::Threads)
	mh_c89(mh_resolver_posix)
endif()

if(WIN32)
	add_library(mh_resolver_windows STATIC
		src/backends/network/windows_resolver.c)
	target_link_libraries(mh_resolver_windows
		PUBLIC
			mh_resolver
		PRIVATE
			ws2_32)
	mh_c89(mh_resolver_windows)
endif()

if((UNIX OR WIN32) AND NOT EMSCRIPTEN)
	add_library(mh_udp_challenge STATIC src/backends/network/udp_challenge.c)
	if(WIN32)
		target_sources(mh_udp_challenge PRIVATE
			src/backends/network/udp_random_windows.c)
		target_link_libraries(mh_udp_challenge PRIVATE bcrypt)
	else()
		target_sources(mh_udp_challenge PRIVATE
			src/backends/network/udp_random_posix.c)
	endif()
	target_link_libraries(mh_udp_challenge PRIVATE mh_udp_framing mh_sha256)
	mh_c89(mh_udp_challenge)

	add_library(mh_transport_udp STATIC
		src/backends/network/udp_transport.c
		src/backends/network/udp_target.c
		src/backends/network/udp_connection.c
		src/backends/network/udp_protocol.c
		src/backends/network/udp_discovery.c)

	foreach(protocol IN LISTS MH_NETWORK_PROTOCOLS)
		target_sources(mh_transport_udp PRIVATE
			src/backends/network/udp_${protocol}.c)
	endforeach()

	target_link_libraries(mh_transport_udp
		PUBLIC
			mh_transport_dispatch
		PRIVATE
			mh_network_build
			mh_udp
			mh_udp_description
			mh_udp_challenge
			mh_reliable_delivery
			mh_net_discovery
			mh_resolver
			mh_clock_source)
	mh_c89(mh_transport_udp)
endif()
