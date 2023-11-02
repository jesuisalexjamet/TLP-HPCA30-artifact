file(
	GLOB_RECURSE
	L1D_BERTI_PREFETCHER_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(LIBRARY_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin/prefetchers")

add_library(l1d_berti SHARED ${L1D_BERTI_PREFETCHER_SOURCES})
target_link_libraries(l1d_berti champsim_internals)
