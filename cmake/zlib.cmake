mh_dependency_version(zlib mh_zlib_version)
find_package(ZLIB ${mh_zlib_version} EXACT MODULE QUIET)
if(NOT ZLIB_FOUND)
	# Older FindZLIB modules omit two-part versions and MinGW static names.
	find_package(ZLIB ${mh_zlib_version} EXACT CONFIG REQUIRED COMPONENTS static)
	add_library(ZLIB::ZLIB ALIAS ZLIB::ZLIBSTATIC)
endif()
