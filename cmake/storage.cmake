add_library(mh_storage STATIC src/engine/storage/storage.c)
target_include_directories(mh_storage PUBLIC src)
target_link_libraries(mh_storage PUBLIC mh_error)
mh_c89(mh_storage)

add_library(mh_resource_source STATIC
	src/engine/storage/resource.c
	src/engine/storage/asset_path.c)
target_include_directories(mh_resource_source PUBLIC src)
target_link_libraries(mh_resource_source PUBLIC mh_error mh_text)
mh_c89(mh_resource_source)

add_library(mh_optical_disc STATIC src/engine/storage/optical.c)
target_link_libraries(mh_optical_disc PUBLIC mh_resource_source)
mh_c89(mh_optical_disc)

add_library(mh_resource_digest STATIC src/engine/storage/digest.c)
target_link_libraries(mh_resource_digest PUBLIC mh_resource_source mh_sha256)
mh_c89(mh_resource_digest)

add_library(mh_user_data STATIC
	src/engine/storage/user_data.c
	src/engine/storage/user_root.c)
target_include_directories(mh_user_data PUBLIC src)
target_link_libraries(mh_user_data PUBLIC mh_error)
mh_c89(mh_user_data)

add_library(mh_settings_storage STATIC src/engine/storage/settings_file.c)
target_link_libraries(mh_settings_storage PUBLIC mh_settings mh_user_data)
mh_c89(mh_settings_storage)

add_library(mh_data_directory STATIC
	src/engine/storage/data_directory.c
	src/engine/storage/data_stream.c)
target_include_directories(mh_data_directory PUBLIC src)
target_link_libraries(mh_data_directory PUBLIC mh_error)
mh_c89(mh_data_directory)

add_library(mh_address_store STATIC src/engine/storage/address_store.c)
target_link_libraries(mh_address_store PUBLIC mh_settings mh_data_directory)
mh_c89(mh_address_store)

add_library(mh_control_store STATIC
	src/engine/storage/control_store.c
	src/engine/storage/control_save.c)
target_link_libraries(mh_control_store PUBLIC
	mh_settings
	mh_data_directory
	mh_resource_source)
mh_c89(mh_control_store)

add_library(mh_highscore_storage STATIC src/engine/storage/highscore_file.c)
target_link_libraries(mh_highscore_storage PUBLIC mh_sim mh_user_data)
mh_c89(mh_highscore_storage)

add_library(mh_record_storage STATIC src/engine/storage/record_file.c)
target_link_libraries(mh_record_storage PUBLIC mh_assets mh_user_data)
mh_c89(mh_record_storage)

add_library(mh_track_flag_storage STATIC
	src/engine/storage/track_flag_file.c
	src/engine/storage/track_flag_save.c)
target_link_libraries(mh_track_flag_storage PUBLIC
	mh_assets
	mh_user_data
	mh_resource_source
	mh_data_directory)
mh_c89(mh_track_flag_storage)

add_library(mh_league_storage STATIC
	src/engine/storage/league_codec.c
	src/engine/storage/league_file.c
	src/engine/storage/league_store.c)
target_link_libraries(mh_league_storage PUBLIC
	mh_sim
	mh_user_data
	mh_data_directory)
mh_c89(mh_league_storage)

add_library(mh_demo_storage STATIC src/engine/storage/demo_file.c)
target_link_libraries(mh_demo_storage PUBLIC mh_demo mh_data_directory)
mh_c89(mh_demo_storage)

add_library(mh_recording_paths STATIC src/engine/storage/recording_path.c)
target_link_libraries(mh_recording_paths PUBLIC
	mh_user_data
	mh_data_directory
	mh_text)
mh_c89(mh_recording_paths)

add_library(mh_recording_catalog STATIC src/engine/storage/recording_catalog.c)
target_link_libraries(mh_recording_catalog PUBLIC
	mh_ui
	mh_data_directory
	mh_resource_source
	mh_user_data)
mh_c89(mh_recording_catalog)

add_library(mh_picture_storage STATIC
	src/engine/storage/picture_store.c
	src/engine/storage/picture_path.c)
target_link_libraries(mh_picture_storage PUBLIC
	mh_assets
	mh_data_directory
	mh_user_data)
mh_c89(mh_picture_storage)

add_library(mh_resource_directory STATIC
	src/engine/storage/resource_directory.c)
target_link_libraries(mh_resource_directory PUBLIC
	mh_resource_source
	mh_data_directory)
mh_c89(mh_resource_directory)

add_library(mh_resource_overlay STATIC src/engine/storage/overlay.c)
target_link_libraries(mh_resource_overlay PUBLIC mh_resource_directory mh_text)
mh_c89(mh_resource_overlay)

add_library(mh_vfs STATIC src/engine/storage/vfs.c)
target_link_libraries(mh_vfs PUBLIC mh_resource_overlay)
mh_c89(mh_vfs)

if(WIN32)
	add_library(mh_windows_text STATIC src/backends/text/windows.c)
	target_link_libraries(mh_windows_text PUBLIC mh_error)
	mh_c89(mh_windows_text)
endif()

# One disk-backed implementation, assembled by storage responsibility.
if(WIN32)
	set(filesystem_platform windows)
else()
	set(filesystem_platform posix)
endif()

add_library(mh_filesystem_io STATIC
	src/backends/storage/filesystem_${filesystem_platform}_file.c)

if(WIN32)
	target_sources(mh_filesystem_io PRIVATE
		src/backends/storage/filesystem_windows_path.c
		src/backends/storage/filesystem_windows_handle.c
		src/backends/storage/filesystem_windows_temporary.c
		src/backends/storage/filesystem_windows_catalog.c)
	target_link_libraries(mh_filesystem_io PRIVATE ntdll mh_windows_text)
endif()
target_link_libraries(mh_filesystem_io PUBLIC mh_error)
mh_c89(mh_filesystem_io)

add_library(mh_file_read_filesystem STATIC
	src/backends/storage/filesystem_file_read.c)
target_link_libraries(mh_file_read_filesystem
	PUBLIC
		mh_error
	PRIVATE
		mh_filesystem_io)
mh_c89(mh_file_read_filesystem)

add_library(mh_storage_filesystem STATIC
	src/backends/storage/filesystem_persistence.c)
target_link_libraries(mh_storage_filesystem PUBLIC mh_storage)
mh_c89(mh_storage_filesystem)

add_library(mh_resource_filesystem STATIC
	src/backends/storage/filesystem_resource.c)
target_link_libraries(mh_resource_filesystem
	PUBLIC
		mh_resource_source
	PRIVATE
		mh_filesystem_io)
mh_c89(mh_resource_filesystem)

if(WIN32)
	target_sources(mh_resource_filesystem PRIVATE
		src/backends/storage/filesystem_windows_executable.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_sources(mh_resource_filesystem PRIVATE
		src/backends/storage/filesystem_linux_executable.c)
else()
	target_sources(mh_resource_filesystem PRIVATE
		src/backends/storage/filesystem_executable_unavailable.c)
endif()

if(UNIX OR WIN32)
	add_library(mh_filesystem_platform STATIC
		src/backends/storage/filesystem_${filesystem_platform}_directory.c)
	target_link_libraries(mh_filesystem_platform
		PUBLIC
			mh_error
		PRIVATE
			mh_filesystem_io)
	mh_c89(mh_filesystem_platform)
	target_compile_definitions(mh_resource_filesystem PRIVATE
		MH_FILESYSTEM_PLATFORM=1)
	target_link_libraries(mh_resource_filesystem PRIVATE
		mh_filesystem_platform)
endif()

if(UNIX OR WIN32)
	add_library(mh_file_replace STATIC
		src/backends/storage/filesystem_${filesystem_platform}_file_replace.c)

	if(WIN32)
		target_link_libraries(mh_file_replace PRIVATE mh_filesystem_io)
	endif()
	target_include_directories(mh_file_replace PUBLIC src)
	mh_c89(mh_file_replace)

	add_library(mh_data_directory_filesystem STATIC
		src/backends/storage/filesystem_${filesystem_platform}_data_directory.c
		src/backends/storage/filesystem_${filesystem_platform}_data_stream.c)

	if(EMSCRIPTEN)
		target_sources(mh_data_directory_filesystem PRIVATE
			src/backends/storage/filesystem_emscripten_publish.c)
	elseif(UNIX)
		target_sources(mh_data_directory_filesystem PRIVATE
			src/backends/storage/filesystem_posix_publish.c)
	endif()
	target_link_libraries(mh_data_directory_filesystem PUBLIC
		mh_data_directory)

	if(WIN32)
		target_sources(mh_data_directory_filesystem PRIVATE
			src/backends/storage/filesystem_windows_data_file.c)
		target_link_libraries(mh_data_directory_filesystem PRIVATE
			mh_filesystem_io
			mh_filesystem_platform)
	endif()
	mh_c89(mh_data_directory_filesystem)

	add_library(mh_user_data_filesystem STATIC
		src/backends/storage/filesystem_user_data.c)
	target_link_libraries(mh_user_data_filesystem
		PUBLIC
			mh_user_data
		PRIVATE
			mh_filesystem_platform
			mh_file_replace
			mh_data_directory_filesystem)
	mh_c89(mh_user_data_filesystem)

	target_sources(mh_resource_filesystem PRIVATE
		src/backends/storage/filesystem_${filesystem_platform}_resource_directory.c)
	target_compile_definitions(mh_resource_filesystem PRIVATE
		MH_FILESYSTEM_DATA_DIRECTORY=1)
	target_link_libraries(mh_resource_filesystem PRIVATE
		mh_data_directory_filesystem)
endif()

if(UNIX AND NOT EMSCRIPTEN)
	find_package(Threads REQUIRED)
	add_library(mh_directory_sync STATIC
		src/backends/storage/filesystem_posix_sync.c)
	target_include_directories(mh_directory_sync PUBLIC src)
	target_link_libraries(mh_directory_sync PRIVATE Threads::Threads)
	mh_c89(mh_directory_sync)

	target_compile_definitions(mh_storage_filesystem PRIVATE MH_DIRECTORY_SYNC=1)
	foreach(target mh_storage_filesystem mh_file_replace mh_filesystem_platform
		mh_data_directory_filesystem)
		target_link_libraries(${target} PRIVATE mh_directory_sync)
	endforeach()
endif()

# Hosts can select the whole filesystem backend; consumers retain focused APIs.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32 OR EMSCRIPTEN)
	add_library(mh_tree_filesystem STATIC
		src/backends/storage/filesystem_${filesystem_platform}_tree.c)
	target_link_libraries(mh_tree_filesystem PUBLIC mh_error)

	if(WIN32)
		target_link_libraries(mh_tree_filesystem PRIVATE
			mh_filesystem_io)
	endif()
	mh_c89(mh_tree_filesystem)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32 OR EMSCRIPTEN)
	if(WIN32)
		add_library(mh_staging_filesystem STATIC
			src/backends/storage/filesystem_windows_staging.c
			src/backends/storage/filesystem_windows_staging_cleanup.c)
		target_link_libraries(mh_staging_filesystem PRIVATE
			mh_filesystem_io)
	elseif(EMSCRIPTEN)
		add_library(mh_staging_filesystem STATIC
			src/backends/storage/filesystem_emscripten_staging.c)
	else()
		add_library(mh_staging_filesystem STATIC
			src/backends/storage/filesystem_linux_staging.c)
	endif()
	target_sources(mh_staging_filesystem PRIVATE
		src/backends/storage/filesystem_staging.c)
	target_link_libraries(mh_staging_filesystem
		PUBLIC
			mh_error
		PRIVATE
			mh_tree_filesystem)
	mh_c89(mh_staging_filesystem)
endif()

add_library(mh_filesystem INTERFACE)
target_link_libraries(mh_filesystem INTERFACE
	mh_resource_filesystem
	mh_storage_filesystem)

if(TARGET mh_user_data_filesystem)
	target_link_libraries(mh_filesystem INTERFACE mh_user_data_filesystem)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	add_library(mh_optical_linux STATIC
		src/backends/storage/optical_linux_disc.c)
	target_link_libraries(mh_optical_linux PUBLIC mh_optical_disc)
	mh_c89(mh_optical_linux)
endif()

if(WIN32)
	add_library(mh_optical_windows STATIC
		src/backends/storage/optical_windows_disc.c)
	target_link_libraries(mh_optical_windows PUBLIC mh_optical_disc)
	mh_c89(mh_optical_windows)
endif()

add_library(mh_resource_memory STATIC src/backends/storage/memory_resource.c)
target_link_libraries(mh_resource_memory PUBLIC
	mh_resource_source
	mh_resource_directory
	mh_data_directory)
mh_c89(mh_resource_memory)

add_library(mh_user_data_memory STATIC
	src/backends/storage/memory_user_data.c
	src/backends/storage/memory_data_directory.c)
target_link_libraries(mh_user_data_memory PUBLIC mh_user_data mh_data_directory)
mh_c89(mh_user_data_memory)

add_library(mh_resource_zip STATIC
	src/backends/storage/zip_index.c
	src/backends/storage/zip_inflate.c
	src/backends/storage/zip_resource.c)
target_link_libraries(mh_resource_zip
	PUBLIC
		mh_resource_source
	PRIVATE
		mh_resource_directory
		mh_core
		ZLIB::ZLIB)
mh_c89(mh_resource_zip)
mh_check_unsigned_lengths(mh_resource_zip)

add_library(mh_content_prepare STATIC
	src/engine/assets/prepare_demo_audio.c
	src/engine/assets/prepare_demo_car.c
	src/engine/assets/prepare_demo_catalog.c
	src/engine/assets/prepare_demo_collision.c
	src/engine/assets/prepare_demo_drivers.c
	src/engine/assets/prepare_demo_font.c
	src/engine/assets/prepare_demo_horn.c
	src/engine/assets/prepare_demo_loading.c
	src/engine/assets/prepare_demo_results.c
	src/engine/assets/prepare_demo_track.c
	src/engine/assets/prepare_cdrdao.c
	src/engine/assets/prepare_disc_layout.c
	src/engine/assets/prepare_cue.c
	src/engine/assets/prepare_cd_audio.c
	src/engine/assets/prepare_unpack.c)
target_link_libraries(mh_content_prepare
	PUBLIC
		mh_demo_presentation
		mh_demo_definitions
		mh_retro_executable
		mh_collision_executable
		mh_resource_digest
		mh_archive
		mh_user_data
		mh_data_directory
	PRIVATE
		mh_core
		mh_assets)
mh_c89(mh_content_prepare)

add_library(mh_archive STATIC
	src/engine/archive/archive.c
	src/engine/archive/disc.c
	src/engine/archive/iso.c
	src/engine/archive/path.c)
target_link_libraries(mh_archive
	PUBLIC
		mh_error
		mh_resource_source
	PRIVATE
		mh_core)
mh_c89(mh_archive)
mh_check_unsigned_lengths(mh_archive)

add_library(mh_zip_write STATIC
	src/engine/archive/zip_write.c
	src/engine/archive/zip_deflate.c)
target_link_libraries(mh_zip_write
	PUBLIC
		mh_resource_source
		mh_data_directory
	PRIVATE
		mh_core
		ZLIB::ZLIB)
mh_c89(mh_zip_write)
mh_check_unsigned_lengths(mh_zip_write)

if(MH_BUILD_EXTRACT AND "installshield" IN_LIST MH_IMPORT_FORMATS)
	find_path(MH_UNSHIELD_INCLUDE_DIR NAMES libunshield.h)
	find_library(MH_UNSHIELD_LIBRARY NAMES unshield)
	mark_as_advanced(MH_UNSHIELD_INCLUDE_DIR MH_UNSHIELD_LIBRARY)

	if(MH_UNSHIELD_INCLUDE_DIR AND MH_UNSHIELD_LIBRARY)
		add_library(mh_unshield UNKNOWN IMPORTED GLOBAL)
		set_target_properties(mh_unshield PROPERTIES
			IMPORTED_LOCATION "${MH_UNSHIELD_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES
				"${MH_UNSHIELD_INCLUDE_DIR}"
			INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)

		add_library(mh_archive_unshield STATIC
			src/backends/archive/unshield_archive.c
			src/backends/archive/unshield_memory.c
			src/backends/archive/unshield_sfx.c
			src/backends/archive/unshield_zip.c)
		target_link_libraries(mh_archive_unshield
			PUBLIC
				mh_archive
			PRIVATE
				mh_unshield
				mh_core)
		mh_c89(mh_archive_unshield)
		mh_check_unsigned_lengths(mh_archive_unshield)
		message(STATUS
			"InstallShield archive decoder: ${MH_UNSHIELD_LIBRARY}")
	else()
		message(STATUS
			"libunshield development files not found; InstallShield extraction disabled")
	endif()
endif()

if(MH_BUILD_EXTRACT AND NOT TARGET mh_archive_unshield)
	add_library(mh_archive_unshield STATIC
		src/backends/archive/unshield_unavailable.c)
	target_link_libraries(mh_archive_unshield PUBLIC mh_archive)
	mh_c89(mh_archive_unshield)
endif()

if(MH_BUILD_EXTRACT)
	add_library(mh_archive_mscab STATIC)

	if("mscab" IN_LIST MH_IMPORT_FORMATS)
		target_sources(mh_archive_mscab PRIVATE
			src/backends/archive/mscab_archive.c)
	else()
		target_sources(mh_archive_mscab PRIVATE
			src/backends/archive/mscab_unavailable.c)
	endif()
	target_link_libraries(mh_archive_mscab
		PUBLIC
			mh_archive
		PRIVATE
			mh_core
			ZLIB::ZLIB)
	mh_c89(mh_archive_mscab)
	mh_check_unsigned_lengths(mh_archive_mscab)
endif()
