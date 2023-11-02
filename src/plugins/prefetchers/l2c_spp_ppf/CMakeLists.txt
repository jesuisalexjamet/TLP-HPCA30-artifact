file(
	GLOB_RECURSE
	L2C_SPP_PPF_PREFETCHER_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(LIBRARY_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin/prefetchers")

add_library(l2c_spp_ppf SHARED ${L2C_SPP_PPF_PREFETCHER_SOURCES})
target_link_libraries(l2c_spp_ppf champsim_internals)
