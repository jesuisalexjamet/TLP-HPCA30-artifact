file(
	GLOB_RECURSE
	CHAMPSIM_INTERNALS_SOURCES
	${CMAKE_SOURCE_DIR}/src/instrumentations/*.cc
	${CMAKE_CURRENT_SOURCE_DIR}/*.cc

	# Adding branch predictor code.
	${CMAKE_SOURCE_DIR}/src/branch_predictors/hashed_perceptron/*.cc

	# Adding prefetcher codes.
	${CMAKE_SOURCE_DIR}/src/prefetchers/l1i_no/*.cc
	${CMAKE_SOURCE_DIR}/src/prefetchers/l1d_no/*.cc
	${CMAKE_SOURCE_DIR}/src/prefetchers/l2c_no/*.cc
	${CMAKE_SOURCE_DIR}/src/prefetchers/llc_no/*.cc

	# Adding LLC replacement policy code.
	${CMAKE_SOURCE_DIR}/src/replacement_policies/lru/*.cc
)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_library(champsim_internals SHARED ${CHAMPSIM_INTERNALS_SOURCES})

# Adding Boost libraries to the target.
target_link_libraries(champsim_internals Boost::program_options Boost::filesystem Boost::system dl)
