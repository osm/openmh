option(MH_BUILD_NETWORK "Build native LAN/INET client networking" OFF)
option(MH_BUILD_SERVER "Build the headless dedicated server" OFF)
option(MH_BUILD_EXTRACT "Build the standalone asset extractor" OFF)

if(MH_BUILD_NETWORK OR MH_BUILD_SERVER)
	message(FATAL_ERROR
		"WebAssembly builds do not support native UDP networking or the dedicated server; use MH_ENABLE_WEBRTC for browser multiplayer")
endif()

# Race operations keep transactional copies of whole fields on the stack.
# Match a typical native stack rather than Emscripten's 64 KiB default.
# Keep overflow checks active: the linear-memory stack has no guard page.
add_link_options(-sSTACK_SIZE=8388608 -sSTACK_OVERFLOW_CHECK=2
	-sALLOW_MEMORY_GROWTH=1)
