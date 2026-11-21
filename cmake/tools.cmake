# Shared diagnostic channel output; no runtime/network transport ownership.
add_library(mh_tool_vehicle_output STATIC src/tools/network/vehicle_output.c)
target_include_directories(mh_tool_vehicle_output PUBLIC src)
mh_c89(mh_tool_vehicle_output)

add_executable(mh-settings-info src/tools/settings/info.c)
target_link_libraries(mh-settings-info PRIVATE mh_settings)
mh_c89(mh-settings-info)

add_executable(mh-demo-info src/tools/assets/demo_info.c)
target_link_libraries(mh-demo-info PRIVATE
	mh_file_read_filesystem
	mh_net
	mh_tool_vehicle_output)
mh_c89(mh-demo-info)

add_executable(mh-env-info src/tools/assets/env_info.c)
target_link_libraries(mh-env-info PRIVATE mh_file_read_filesystem mh_audio)
mh_c89(mh-env-info)

add_executable(mh-object-info src/tools/assets/object_info.c)
target_link_libraries(mh-object-info PRIVATE mh_assets)
mh_c89(mh-object-info)

add_executable(mh-wave-info src/tools/assets/wave_info.c)
target_link_libraries(mh-wave-info PRIVATE mh_file_read_filesystem mh_assets)
mh_c89(mh-wave-info)

add_executable(mh-horn-info src/tools/assets/horn_info.c)
target_link_libraries(mh-horn-info PRIVATE mh_file_read_filesystem mh_assets)
mh_c89(mh-horn-info)

add_executable(mh-ui-info src/tools/assets/ui_info.c)
target_link_libraries(mh-ui-info PRIVATE mh_file_read_filesystem mh_assets)
mh_c89(mh-ui-info)

add_executable(mh-spr-info src/tools/assets/spr_info.c)
target_link_libraries(mh-spr-info PRIVATE mh_file_read_filesystem mh_assets)
mh_c89(mh-spr-info)

add_executable(mh-myo-info src/tools/assets/myo_info.c)
target_link_libraries(mh-myo-info PRIVATE mh_assets)
mh_c89(mh-myo-info)

add_executable(mh-asset-check src/tools/assets/asset_check.c)
target_link_libraries(mh-asset-check PRIVATE mh_file_read_filesystem mh_assets)
mh_c89(mh-asset-check)

add_executable(mh-resource-check src/tools/assets/resource_check.c)
target_link_libraries(mh-resource-check PRIVATE
	mh_file_read_filesystem
	mh_assets
	mh_settings
	mh_ui)
mh_c89(mh-resource-check)

add_executable(mh-tbl-decode src/tools/assets/tbl_decode.c)
target_link_libraries(mh-tbl-decode PRIVATE mh_file_read_filesystem mh_assets)
mh_c89(mh-tbl-decode)

add_executable(mh-scene-info src/tools/assets/scene_info.c)
target_link_libraries(mh-scene-info PRIVATE mh_file_read_filesystem mh_assets)
mh_c89(mh-scene-info)

add_executable(mh-light-info src/tools/assets/light_info.c)
target_link_libraries(mh-light-info PRIVATE mh_file_read_filesystem mh_assets)
mh_c89(mh-light-info)

add_executable(mh-col-info src/tools/assets/col_info.c)
target_link_libraries(mh-col-info PRIVATE mh_file_read_filesystem mh_sim)
mh_c89(mh-col-info)

if(TARGET mh_diagnostic_network)
	add_executable(mh-net-discover src/tools/network/discover.c)
	target_link_libraries(mh-net-discover PRIVATE
		mh_diagnostic_network
		mh_host_network
		mh_host_clock)
	mh_c89(mh-net-discover)

	add_executable(mh-net-lobby src/tools/network/lobby.c)
	target_link_libraries(mh-net-lobby PRIVATE
		mh_client_diagnostics
		mh_diagnostic_network
		mh_host_network
		mh_host_clock)
	mh_c89(mh-net-lobby)
endif()

if(MH_BUILD_CLIENT)
	add_executable(mh-model-viewer src/tools/render/model_viewer.c)
	target_link_libraries(mh-model-viewer PRIVATE
		mh_file_read_filesystem
		mh_diagnostic_runtime
		mh_image_fallback
		mh_image_pdi)
	mh_c89(mh-model-viewer)
endif()

if(TARGET mh_diagnostic_video)
	add_executable(mh-movie-play src/tools/video/play.c)
	target_link_libraries(mh-movie-play PRIVATE mh_diagnostic_video)
	mh_c89(mh-movie-play)
endif()
