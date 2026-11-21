if(BUILD_TESTING)
	add_executable(mh-demo-session-check tests/extract/demo_session.c)
	target_link_libraries(mh-demo-session-check PRIVATE
		mh_catalog_fallback
		mh_horn_fallback
		mh_material_mat_srf
		mh_environment_esd
		mh_collision_fallback
		mh_model_myo
		mh_world_myw
		mh_atlas_remap
		mh_navigation_mot_dat
		mh_animation_lightwave
		mh_font_fallback
		mh_sprite_spr
		mh_audio_fallback
		mh_image_fallback
		mh_image_pdi
		mh_single_setup
		mh_audio_assets
		mh_resource_filesystem)
	mh_c89(mh-demo-session-check)

	add_executable(test-demo-audio tests/extract/demo_audio.c)
	target_link_libraries(test-demo-audio PRIVATE
		mh_horn_fallback
		mh_audio_fallback
		mh_content_prepare
		mh_audio_assets
		mh_resource_memory
		mh_user_data_memory
		mh_resource_filesystem)
	mh_c89(test-demo-audio)
	add_test(NAME content-demo-audio COMMAND test-demo-audio)

	add_executable(test-demo-drivers tests/extract/demo_drivers.c)
	target_link_libraries(test-demo-drivers PRIVATE
		mh_catalog_fallback
		mh_horn_fallback
		mh_content_prepare
		mh_menus_app
		mh_resource_memory
		mh_user_data_memory
		mh_resource_filesystem)
	mh_c89(test-demo-drivers)
	add_test(NAME content-demo-drivers COMMAND test-demo-drivers)

	add_executable(test-demo-horn tests/extract/demo_horn.c)
	target_link_libraries(test-demo-horn PRIVATE
		mh_horn_fallback
		mh_audio_fallback
		mh_content_prepare
		mh_audio_assets
		mh_resource_memory
		mh_user_data_memory
		mh_resource_filesystem)
	mh_c89(test-demo-horn)
	add_test(NAME content-demo-horn COMMAND test-demo-horn)

	add_executable(test-demo-finish tests/extract/demo_finish.c)
	target_link_libraries(test-demo-finish PRIVATE
		mh_application
		mh_resource_memory)
	mh_c89(test-demo-finish)
	add_test(NAME content-optional-finish COMMAND test-demo-finish)

	add_executable(test-demo-results tests/extract/demo_results.c)
	target_link_libraries(test-demo-results PRIVATE
		mh_font_fallback
		mh_sprite_spr
		mh_image_fallback
		mh_content_prepare
		mh_ui_assets
		mh_resource_memory
		mh_user_data_memory
		mh_resource_filesystem
		mh_render_software)
	mh_c89(test-demo-results)
	add_test(NAME content-demo-results COMMAND test-demo-results)

	add_executable(test-demo-loading tests/extract/demo_loading.c)
	target_link_libraries(test-demo-loading PRIVATE
		mh_content_prepare
		mh_resource_memory
		mh_user_data_memory
		mh_ui_assets)
	mh_c89(test-demo-loading)
	add_test(NAME content-demo-loading COMMAND test-demo-loading)

	add_executable(test-demo-font tests/extract/demo_font.c)
	target_link_libraries(test-demo-font PRIVATE
		mh_content_prepare
		mh_file_read_filesystem)
	mh_c89(test-demo-font)
	add_test(NAME content-demo-font COMMAND test-demo-font)

	add_executable(test-demo-car tests/extract/demo_car.c)
	target_link_libraries(test-demo-car PRIVATE
		mh_content_prepare
		mh_assets
		mh_file_read_filesystem)
	mh_c89(test-demo-car)
	add_test(NAME content-demo-car COMMAND test-demo-car)

	add_executable(test-content-probe tests/assets/content_probe.c)
	target_link_libraries(test-content-probe PRIVATE
		mh_content
		mh_resource_memory
		mh_resource_filesystem)
	mh_c89(test-content-probe)
	add_test(NAME content-probe COMMAND test-content-probe)

	add_executable(test-content-startup tests/integration/content_startup.c)
	target_link_libraries(test-content-startup PRIVATE
		mh_menus_app
		mh_resource_memory
		mh_resource_filesystem)
	mh_c89(test-content-startup)
	add_test(NAME content-startup-paths COMMAND test-content-startup)

	add_executable(test-content-saves tests/integration/content_saves.c)
	target_link_libraries(test-content-saves PRIVATE
		mh_menus_app
		mh_user_data_memory)
	mh_c89(test-content-saves)
	add_test(NAME content-edition-saves COMMAND test-content-saves)

	add_executable(test-extract-iso tests/extract/iso.c)
	target_link_libraries(test-extract-iso PRIVATE
		mh_archive
		mh_resource_memory)
	mh_c89(test-extract-iso)
	add_test(NAME content-iso-archive COMMAND test-extract-iso)

	add_executable(test-cdrdao tests/extract/cdrdao.c)
	target_link_libraries(test-cdrdao PRIVATE mh_content_prepare)
	mh_c89(test-cdrdao)
	add_test(NAME content-cdrdao-layout COMMAND test-cdrdao)

	add_executable(test-extract-disc tests/extract/disc.c)
	target_link_libraries(test-extract-disc PRIVATE
		mh_content_prepare
		mh_resource_memory
		mh_user_data_memory)
	mh_c89(test-extract-disc)
	add_test(NAME content-disc-audio COMMAND test-extract-disc)

	add_executable(test-optical-disc tests/extract/optical.c)
	target_link_libraries(test-optical-disc PRIVATE
		mh_optical_disc
		mh_content_prepare
		mh_user_data_memory)
	mh_c89(test-optical-disc)
	add_test(NAME content-optical-disc COMMAND test-optical-disc)

	add_executable(test-unpack tests/extract/unpack.c)
	target_link_libraries(test-unpack PRIVATE
		mh_content_prepare
		mh_user_data_memory)
	mh_c89(test-unpack)
	add_test(NAME content-unpack COMMAND test-unpack)

	add_executable(test-archive tests/extract/archive.c)
	target_link_libraries(test-archive PRIVATE mh_archive)
	mh_c89(test-archive)
	add_test(NAME content-archive-contract COMMAND test-archive)

	add_executable(test-sha256 tests/core/sha256.c)
	target_link_libraries(test-sha256 PRIVATE mh_sha256)
	mh_c89(test-sha256)
	add_test(NAME content-sha256 COMMAND test-sha256)

	add_executable(test-content-packages tests/extract/packages.c)
	target_link_libraries(test-content-packages PRIVATE
		mh_extract_source
		mh_resource_memory)
	mh_c89(test-content-packages)
	add_test(NAME content-package-identity COMMAND test-content-packages)
endif()

if(BUILD_TESTING AND TARGET mh_optical_linux)
	add_executable(test-optical-linux tests/extract/optical_linux.c)
	target_link_libraries(test-optical-linux PRIVATE mh_optical_linux)
	target_link_options(test-optical-linux PRIVATE
		-Wl,--wrap=open
		-Wl,--wrap=close
		-Wl,--wrap=stat
		-Wl,--wrap=fstat
		-Wl,--wrap=ioctl)
	mh_c89(test-optical-linux)
	add_test(NAME content-optical-linux COMMAND test-optical-linux)
endif()

if(BUILD_TESTING AND TARGET openmotorex AND TARGET mh_archive_unshield AND
	TARGET mh_staging_filesystem AND Python3_Interpreter_FOUND AND
	CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
	# Keep the actual CLI and transaction pipeline; only patch decoding is fake.
	get_target_property(mh_extract_test_sources openmotorex SOURCES)
	get_target_property(mh_extract_test_libraries openmotorex
		LINK_LIBRARIES)

	add_executable(test-extract-sources
		${mh_extract_test_sources}
		tests/extract/source_patches.c)
	target_link_libraries(test-extract-sources PRIVATE
		${mh_extract_test_libraries})
	target_link_options(test-extract-sources PRIVATE
		-Wl,--wrap=mh_extract_patch_identify
		-Wl,--wrap=mh_extract_patch_validate
		-Wl,--wrap=mh_extract_patch_apply)
	mh_c89(test-extract-sources)
	add_test(NAME content-multiple-sources COMMAND "${Python3_EXECUTABLE}"
		"${CMAKE_CURRENT_SOURCE_DIR}/tests/extract/multiple_sources.py"
		"$<TARGET_FILE:test-extract-sources>"
			"$<TARGET_FILE:openmotorex>")
	set_tests_properties(content-multiple-sources PROPERTIES TIMEOUT 60)
endif()

if(BUILD_TESTING AND WIN32 AND MINGW AND TARGET mh_optical_windows)
	add_executable(test-optical-windows tests/extract/optical_windows.c)
	target_link_libraries(test-optical-windows PRIVATE mh_optical_windows)
	target_link_options(test-optical-windows PRIVATE
		-Wl,--wrap=__imp_GetDriveTypeW
		-Wl,--wrap=__imp_CreateFileW
		-Wl,--wrap=__imp_CloseHandle
		-Wl,--wrap=__imp_DeviceIoControl
		-Wl,--wrap=__imp_SetFilePointerEx
		-Wl,--wrap=__imp_ReadFile)
	mh_c89(test-optical-windows)
	add_test(NAME content-optical-windows COMMAND test-optical-windows)
	set_tests_properties(content-optical-windows PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING AND WIN32 AND TARGET mh_tree_filesystem)
	add_executable(test-extract-tree-windows tests/extract/tree_windows.c)
	target_link_libraries(test-extract-tree-windows PRIVATE
		mh_tree_filesystem)
	mh_c89(test-extract-tree-windows)
	add_test(NAME content-windows-tree-copy COMMAND
		test-extract-tree-windows)
	set_tests_properties(content-windows-tree-copy PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING AND WIN32 AND MINGW AND TARGET mh_staging_filesystem)
	add_executable(test-extract-staging-windows
		tests/extract/staging_windows.c)
	target_link_libraries(test-extract-staging-windows PRIVATE
		mh_staging_filesystem
		mh_filesystem_io)
	target_link_options(test-extract-staging-windows PRIVATE
		"-Wl,--wrap=mh_windows_rename_handle")

	if(MINGW)
		target_sources(test-extract-staging-windows PRIVATE
			tests/support/windows_reopen.c)
		target_link_options(test-extract-staging-windows PRIVATE
			"-Wl,--wrap=__imp_ReOpenFile")
	endif()
	mh_c89(test-extract-staging-windows)
	add_test(NAME content-windows-publication COMMAND
		test-extract-staging-windows)
	set_tests_properties(content-windows-publication PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING AND TARGET mh_staging_filesystem AND NOT WIN32 AND
	NOT EMSCRIPTEN)

	add_executable(test-extract-staging tests/extract/staging.c)
	target_link_libraries(test-extract-staging PRIVATE
		mh_staging_filesystem)
	mh_c89(test-extract-staging)
	add_test(NAME content-output-publication COMMAND test-extract-staging)
endif()

if(BUILD_TESTING AND TARGET mh_unshield)
	add_executable(test-demo-sfx tests/extract/sfx.c)
	target_link_libraries(test-demo-sfx PRIVATE mh_archive_unshield)
	mh_c89(test-demo-sfx)
	add_test(NAME content-demo-sfx COMMAND test-demo-sfx)

	add_executable(test-zip-cabinet tests/extract/zip.c)
	target_link_libraries(test-zip-cabinet PRIVATE mh_archive_unshield)
	mh_c89(test-zip-cabinet)
	add_test(NAME content-zip-cabinet COMMAND test-zip-cabinet)
endif()

if(BUILD_TESTING AND TARGET mh_archive_mscab AND
	"mscab" IN_LIST MH_IMPORT_FORMATS)

	add_executable(test-mscab tests/extract/mscab.c)
	target_link_libraries(test-mscab PRIVATE mh_archive_mscab)
	mh_c89(test-mscab)
	add_test(NAME content-mscab COMMAND test-mscab)
endif()

if(BUILD_TESTING AND TARGET mh_staging_filesystem AND NOT WIN32 AND
	NOT EMSCRIPTEN)

	add_executable(test-extract-update tests/extract/staging_update.c)
	target_link_libraries(test-extract-update PRIVATE mh_staging_filesystem)
	mh_c89(test-extract-update)
	add_test(NAME content-update-publication COMMAND test-extract-update)
endif()

if(BUILD_TESTING)
	add_executable(test-extract-path tests/extract/path.c)
	target_link_libraries(test-extract-path PRIVATE mh_extract_path)
	mh_c89(test-extract-path)
	add_test(NAME extract-path-joining COMMAND test-extract-path)
endif()
