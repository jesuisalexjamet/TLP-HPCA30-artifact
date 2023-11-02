file(
	GLOB_RECURSE
	L1D_BERTI_ISO_PREFETCHER_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(LIBRARY_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin/prefetchers")

add_library(l1d_berti_iso SHARED ${L1D_BERTI_ISO_PREFETCHER_SOURCES})
target_link_libraries(l1d_berti_iso champsim_internals)
