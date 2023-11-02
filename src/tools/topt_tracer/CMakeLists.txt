file(
	GLOB_RECURSE
	CHAMPSIM_TOOLS_TOPT_TRACER
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_executable(topt_tracer ${CHAMPSIM_TOOLS_TOPT_TRACER})

# Adding Boost libraries to the target.
target_link_libraries(topt_tracer Boost::program_options Boost::filesystem)
