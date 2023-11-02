file(
	GLOB_RECURSE
	LLC_LRU_REPLACEMENT_POLICY_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(LIBRARY_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin/replacements")

add_library(llc_lru SHARED ${LLC_LRU_REPLACEMENT_POLICY_SOURCES})
target_link_libraries(llc_lru champsim_internals)
