file(
	GLOB_RECURSE
	L1D_NO_PREFETCHER_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(LIBRARY_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin/prefetchers")

add_library(llc_no SHARED ${L1D_NO_PREFETCHER_SOURCES})
target_link_libraries(llc_no champsim_internals)
