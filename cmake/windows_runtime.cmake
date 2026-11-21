# Use the same prepared runtime layout as the Docker exporter.
set(MH_WINDOWS_RUNTIME_DIR "" CACHE PATH
	"Matching Windows runtime directory containing bin/ and licenses.txt")
set(mh_runtime_files)

if(NOT MH_WINDOWS_RUNTIME_DIR OR
	NOT IS_DIRECTORY "${MH_WINDOWS_RUNTIME_DIR}/bin")
	list(APPEND mh_release_unavailable
		"Set MH_WINDOWS_RUNTIME_DIR to a matching prepared runtime with bin/ and licenses.txt")
else()
	file(GLOB mh_runtime_files CONFIGURE_DEPENDS
		"${MH_WINDOWS_RUNTIME_DIR}/bin/*.[dD][lL][lL]")
endif()

set(mh_release_license_bundle "${MH_WINDOWS_RUNTIME_DIR}/licenses.txt")
list(APPEND mh_release_inputs ${mh_runtime_files}
	"${mh_release_license_bundle}")

if(MH_WINDOWS_RUNTIME_DIR AND
	EXISTS "${MH_WINDOWS_RUNTIME_DIR}/dependency-sources.tar.gz")
	set(mh_release_source_archive
		"${MH_WINDOWS_RUNTIME_DIR}/dependency-sources.tar.gz")
endif()
