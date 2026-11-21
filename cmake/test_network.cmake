if(NOT TARGET mh_webrtc_wire)
	return()
endif()

add_executable(test-webrtc-wire tests/network/webrtc_wire.c)
target_link_libraries(test-webrtc-wire PRIVATE mh_webrtc_wire)
mh_c89(test-webrtc-wire)
add_test(NAME webrtc-wire COMMAND test-webrtc-wire)

add_executable(test-webrtc-sdp tests/network/webrtc_sdp.c)
target_link_libraries(test-webrtc-sdp PRIVATE mh_webrtc_wire)
mh_c89(test-webrtc-sdp)
add_test(NAME webrtc-sdp COMMAND test-webrtc-sdp)

add_executable(test-webrtc-sctp tests/network/webrtc_sctp.c)
target_link_libraries(test-webrtc-sctp PRIVATE mh_webrtc_wire)
mh_c89(test-webrtc-sctp)
add_test(NAME webrtc-sctp COMMAND test-webrtc-sctp)

add_executable(test-webrtc-signaling
	tests/network/webrtc_signaling.c)
target_link_libraries(test-webrtc-signaling PRIVATE
	mh_webrtc_signaling)
mh_c89(test-webrtc-signaling)
add_test(NAME webrtc-signaling COMMAND test-webrtc-signaling)

if(TARGET mh_transport_browser)
	add_test(NAME browser-network-bridge COMMAND
		${CMAKE_CROSSCOMPILING_EMULATOR}
		"${CMAKE_SOURCE_DIR}/tests/network/browser_bridge.js"
			"${browser_network_bridge}")

	add_executable(test-webrtc-browser-transport
		tests/network/webrtc_browser_transport.c)
	target_link_libraries(test-webrtc-browser-transport PRIVATE
		mh_transport_browser)
	string(CONCAT browser_exported_functions
		"-sEXPORTED_FUNCTIONS=['_main',"
		"'_mh_browser_transport_check_start',"
		"'_mh_browser_transport_check_step',"
		"'_mh_browser_transport_check_close']")
	target_link_options(test-webrtc-browser-transport PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createBrowserTransportCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_RUNTIME_METHODS=['ccall']"
		"${browser_exported_functions}")
	mh_c89(test-webrtc-browser-transport)

	add_executable(test-mixed-browser-client
		tests/network/mixed_client.c)
	target_link_libraries(test-mixed-browser-client PRIVATE
		mh_transport_browser
		mh_network_session)
	string(CONCAT browser_exported_functions
		"-sEXPORTED_FUNCTIONS=['_main',"
		"'_mh_mixed_client_open',"
		"'_mh_mixed_client_step',"
		"'_mh_mixed_client_close']")
	target_link_options(test-mixed-browser-client PRIVATE
		-sMODULARIZE=1
		-sEXPORT_NAME=createMixedClientCheck
		-sINVOKE_RUN=0
		"-sEXPORTED_RUNTIME_METHODS=['ccall']"
		"${browser_exported_functions}")
	mh_c89(test-mixed-browser-client)
else()
	# Exercise the portable C ownership/delivery boundary with a controlled bridge.
	add_executable(test-browser-transport
		tests/network/browser_transport.c
		src/backends/network/browser_transport.c
		src/backends/network/browser_target.c)
	target_link_libraries(test-browser-transport PRIVATE
		mh_transport_dispatch
		mh_reliable_delivery
		mh_text)
	mh_c89(test-browser-transport)
	add_test(NAME browser-transport COMMAND test-browser-transport)
endif()

if(TARGET mh_transport_webrtc)
	add_executable(test-transport-combined
		tests/network/transport_combined.c)
	target_link_libraries(test-transport-combined PRIVATE
		mh_transport_combined)
	mh_c89(test-transport-combined)
	add_test(NAME transport-combined COMMAND
		test-transport-combined)

	add_executable(test-webrtc-server
		tests/network/webrtc_server.c)
	target_link_libraries(test-webrtc-server PRIVATE
		mh_transport_webrtc)
	mh_c89(test-webrtc-server)
	add_test(NAME webrtc-server COMMAND test-webrtc-server)

	add_executable(test-webrtc-stun-auth
		tests/network/webrtc_stun_auth.c)
	target_link_libraries(test-webrtc-stun-auth PRIVATE
		mh_webrtc_crypto)
	mh_c89(test-webrtc-stun-auth)
	add_test(NAME webrtc-stun-auth COMMAND
		test-webrtc-stun-auth)

	add_executable(test-webrtc-dtls
		tests/network/webrtc_dtls.c)
	target_link_libraries(test-webrtc-dtls PRIVATE
		mh_webrtc_crypto)
	mh_c89(test-webrtc-dtls)
	add_test(NAME webrtc-dtls COMMAND test-webrtc-dtls)

	if(UNIX)
		add_executable(test-webrtc-provider-peer
			tests/network/webrtc_provider_peer.c)
		target_link_libraries(test-webrtc-provider-peer PRIVATE
			mh_transport_webrtc
			mh_clock_system)
		mh_c89(test-webrtc-provider-peer)

		add_executable(test-webrtc-browser-peer
			tests/network/webrtc_browser_peer.c)
		target_link_libraries(test-webrtc-browser-peer PRIVATE
			mh_webrtc_crypto
			OpenSSL::Crypto)
		mh_c89(test-webrtc-browser-peer)
	endif()

	add_executable(test-mixed-native-client
		tests/network/mixed_client.c)
	target_link_libraries(test-mixed-native-client PRIVATE
		mh_transport_udp
		mh_network_session
		mh_clock_system
		mh_arguments)

	if(WIN32)
		target_link_libraries(test-mixed-native-client PRIVATE
			mh_resolver_windows)
	else()
		target_link_libraries(test-mixed-native-client PRIVATE
			mh_resolver_posix)
	endif()
	mh_c89(test-mixed-native-client)

	add_executable(test-signaling-server
		tests/network/signaling_server.c
		src/backends/network/signaling_server.c)
	target_include_directories(test-signaling-server PRIVATE
		src)
	target_link_libraries(test-signaling-server PRIVATE
		mh_webrtc_signaling
		mh_clock_source
		mh_text)
	mh_c89(test-signaling-server)
	add_test(NAME signaling-server COMMAND
		test-signaling-server)
endif()
