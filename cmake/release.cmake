# Only explicitly selected runtime files enter an install or binary archive.
set(mh_release_unavailable)
set(mh_release_inputs)
set(mh_release_notices "${CMAKE_SOURCE_DIR}/LICENSE.md")
set(mh_release_source_archive)
set(mh_release_license_bundle)
set(mh_application_directory .)
set(CPACK_GENERATOR TGZ)

if(TARGET openmotorex AND (("installshield" IN_LIST MH_IMPORT_FORMATS AND
	NOT TARGET mh_unshield) OR NOT TARGET mh_staging_filesystem))
	list(APPEND mh_release_unavailable
		"Selected extractor modules require libunshield and a filesystem staging backend")
endif()

if(WIN32)
	include(cmake/windows_runtime.cmake)
	set(CPACK_GENERATOR ZIP)
endif()

install(SCRIPT "${CMAKE_BINARY_DIR}/check-release.cmake" COMPONENT Runtime)
install(FILES "${CMAKE_BINARY_DIR}/licenses.txt" DESTINATION .
	COMPONENT Runtime)
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_BINARY_DIR}/check-release.cmake")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(MH_LINUX_RUNTIME_DIR "" CACHE PATH
		"Matching Linux runtime directory containing bin/ and licenses.txt")
	configure_file(cmake/package_linux.cmake.in
		"${CMAKE_BINARY_DIR}/package-linux.cmake" @ONLY)
	list(APPEND CPACK_PRE_BUILD_SCRIPTS "${CMAKE_BINARY_DIR}/package-linux.cmake")

	if(MH_LINUX_RUNTIME_DIR AND
		EXISTS "${MH_LINUX_RUNTIME_DIR}/dependency-sources.tar.gz")
		set(mh_release_source_archive
			"${MH_LINUX_RUNTIME_DIR}/dependency-sources.tar.gz")
	endif()
endif()

foreach(application openmotor openmotorsv openmotorex)
	if(TARGET ${application})
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			set_property(TARGET ${application} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
		endif()

		install(TARGETS ${application} RUNTIME DESTINATION
			"${mh_application_directory}" COMPONENT Runtime)
	endif()
endforeach()

if(WIN32 AND mh_runtime_files)
	install(FILES ${mh_runtime_files} DESTINATION . COMPONENT Runtime)
endif()

set(CPACK_PACKAGE_NAME openmh)
set(CPACK_PACKAGE_VERSION "${MH_PACKAGE_VERSION}")
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)"
	mh_release_version "${MH_PACKAGE_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_2}")
set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_3}")
string(TOLOWER
	"openmh-${MH_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
	CPACK_PACKAGE_FILE_NAME)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_INSTALL_CMAKE_PROJECTS
	"${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/")

if(mh_release_source_archive)
	configure_file(cmake/package_sources.cmake.in
		"${CMAKE_BINARY_DIR}/package-dependency-sources.cmake" @ONLY)
	set(CPACK_POST_BUILD_SCRIPTS
		"${CMAKE_BINARY_DIR}/package-dependency-sources.cmake")
endif()

configure_file(cmake/check_release.cmake.in
	"${CMAKE_BINARY_DIR}/check-release.cmake" @ONLY)
include(CPack)
