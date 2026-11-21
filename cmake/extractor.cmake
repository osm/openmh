add_library(mh_extract_path STATIC src/extract/path/join.c)
target_link_libraries(mh_extract_path PUBLIC mh_error)
mh_c89(mh_extract_path)

add_library(mh_extract_source STATIC
	src/extract/source/demo.c
	src/extract/source/packages.c)
target_link_libraries(mh_extract_source PUBLIC mh_content_prepare mh_content)
mh_c89(mh_extract_source)

if(MH_BUILD_EXTRACT)
	add_executable(openmotorex src/extract/cli/main.c)
	target_link_libraries(openmotorex PRIVATE
		mh_build_version
		mh_error
		mh_arguments
		mh_extract_path)

	if(EMSCRIPTEN)
		target_sources(openmotorex PRIVATE src/extract/hosts/browser_main.c)
	elseif(WIN32)
		target_sources(openmotorex PRIVATE
			src/extract/hosts/windows_main.c)
		target_link_libraries(openmotorex PRIVATE mh_filesystem_io)
		target_link_options(openmotorex PRIVATE -municode)
	else()
		target_sources(openmotorex PRIVATE
			src/extract/hosts/posix_main.c)
	endif()

	if(EMSCRIPTEN)
		set_target_properties(openmotorex PROPERTIES SUFFIX ".js")
		target_link_options(openmotorex PRIVATE
			-sMODULARIZE=1
			-sEXPORT_NAME=createOpenMHExtractor
			-sINVOKE_RUN=0
			-sEXIT_RUNTIME=0
			-sENVIRONMENT=worker
			-lworkerfs.js
			"-sEXPORTED_RUNTIME_METHODS=['FS','MEMFS','WORKERFS','ERRNO_CODES','callMain']")
		set(extract_bridge
			"${CMAKE_CURRENT_SOURCE_DIR}/src/extract/hosts/browser_bridge.js")
		target_link_options(openmotorex PRIVATE
			"SHELL:--js-library '${extract_bridge}'")
		set_property(TARGET openmotorex APPEND PROPERTY LINK_DEPENDS
			"${extract_bridge}")

		foreach(file worker.js job.js)
			configure_file("src/extract/hosts/browser_${file}"
				"${CMAKE_BINARY_DIR}/extract-${file}" COPYONLY)
		endforeach()
		configure_file(src/backends/storage/opfs_workspace.js
			"${CMAKE_BINARY_DIR}/extract-workspace.js" COPYONLY)
	endif()
	mh_c89(openmotorex)
endif()

if(TARGET openmotorex)
	if(TARGET mh_user_data_filesystem)
		if(TARGET mh_optical_linux)
			target_sources(openmotorex PRIVATE
				src/extract/hosts/linux_optical.c)
			target_link_libraries(openmotorex PRIVATE
				mh_optical_linux)
		elseif(TARGET mh_optical_windows)
			target_sources(openmotorex PRIVATE
				src/extract/hosts/windows_optical.c)
			target_link_libraries(openmotorex PRIVATE
				mh_optical_windows)
		else()
			target_sources(openmotorex PRIVATE
				src/extract/hosts/optical_unavailable.c)
		endif()
		target_link_libraries(openmotorex PRIVATE mh_optical_disc)
		target_sources(openmotorex PRIVATE
			src/extract/cli/extract.c
			src/extract/progress/report.c
			src/extract/source/input.c
			src/extract/output/package.c
			src/extract/output/entry.c)
		target_link_libraries(openmotorex PRIVATE
			mh_zip_write
			mh_content
			mh_user_data_filesystem
			mh_resource_filesystem
			mh_resource_memory
			mh_asset_loaders)

		if(EMSCRIPTEN)
			target_sources(openmotorex PRIVATE
				src/backends/storage/browser_package_check.c)
			target_link_libraries(openmotorex PRIVATE
				mh_resource_zip)
		endif()
		target_sources(openmotorex PRIVATE
			src/extract/output/compact.c
			src/extract/output/movies.c)
		target_link_libraries(openmotorex PRIVATE
			mh_audio_wave
			mh_audio_ffmpeg_encoding
			mh_video_ffmpeg_encoding
			mh_video_ffmpeg)
	else()
		target_sources(openmotorex PRIVATE
			src/extract/cli/extract_stub.c)
	endif()

	if(TARGET mh_archive_unshield AND TARGET mh_staging_filesystem)
		target_sources(openmotorex PRIVATE
			src/extract/prepare/install.c
			src/extract/prepare/update.c
			src/extract/prepare/demo.c
			src/extract/output/files.c
			src/extract/source/package.c
			src/extract/source/patch.c
			src/extract/source/installer.c
			src/extract/source/demo_unpack.c
			src/extract/prepare/patch.c
			src/extract/source/disc.c
			src/extract/source/drive.c
			src/extract/prepare/retail.c)
		target_link_libraries(openmotorex PRIVATE
			mh_extract_source
			mh_content
			mh_archive_unshield
			mh_staging_filesystem
			mh_archive_mscab
			mh_resource_zip
			mh_user_data_filesystem
			mh_resource_filesystem)
	elseif(TARGET mh_user_data_filesystem)
		target_sources(openmotorex PRIVATE
			src/extract/prepare/install_stub.c)
	endif()
endif()
