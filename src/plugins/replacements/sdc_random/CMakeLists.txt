file(
	GLOB_RECURSE
	SDC_RANDOM_REPLACEMENT_POLICY_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(LIBRARY_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin/replacements")

add_library(sdc_random SHARED ${SDC_RANDOM_REPLACEMENT_POLICY_SOURCES})
target_link_libraries(sdc_random champsim_internals)
