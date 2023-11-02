file(
	GLOB_RECURSE
	CHAMPSIM_SIMULATOR_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_executable(champsim_simulator ${CHAMPSIM_SIMULATOR_SOURCES})
target_link_libraries(champsim_simulator champsim_internals)
