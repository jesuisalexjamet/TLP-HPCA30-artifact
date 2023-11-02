file(
	GLOB_RECURSE
	L1D_IP_STRIDE_PREFETCHER_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(LIBRARY_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin/prefetchers")

add_library(l1d_ip_stride SHARED ${L1D_IP_STRIDE_PREFETCHER_SOURCES})
target_link_libraries(l1d_ip_stride champsim_internals)
