file(
	GLOB_RECURSE
	SDC_NEXT_LINE_PREFETCHER_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(LIBRARY_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin/prefetchers")

add_library(sdc_next_line SHARED ${SDC_NEXT_LINE_PREFETCHER_SOURCES})
target_link_libraries(sdc_next_line champsim_internals)
