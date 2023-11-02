cmake_minimum_required(VERSION 3.10.2)

project(CPUSimulator C CXX)

set(CMAKE_CXX_STANDARD 20)

set(CHAMPSIM_BRANCH_PREDICTOR "hashed_perceptron" CACHE STRING "The branch prediction mechanism used by the simulation.")
set(CHAMPSIM_PREFETCHER_L1I "l1i_no" CACHE STRING "The prefetching mechanism used in the L1I cache.")
set(CHAMPSIM_PREFETCHER_L1D "l1d_no" CACHE STRING "The prefetching mechanism used in the L1D cache.")
set(CHAMPSIM_PREFETCHER_L2C "l2c_no" CACHE STRING "The prefetching mechanism used in the L2C cache.")
set(CHAMPSIM_PREFETCHER_LLC "llc_no" CACHE STRING "The prefetching mechanism used in the LLC cache.")
set(CHAMPSIM_REPLACEMENT_POLICY "lru" CACHE STRING "The replacement policy used in the Last-Level Cache.")

include(cmake/cpu_config.cmake)

# Including 3rd-party packages.
find_package(Boost 1.41.0 REQUIRED
COMPONENTS program_options filesystem system)

# We also have to specify the internals source directory as an include directory.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/internals)

# Adding Boost's header directories.
include_directories(${Boost_INCLUDE_DIRS})

# Modifying the output directory of the binaries.
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/bin/${SIMULATOR_OUTPUT_DIRECTORY}")
set(LIBRARY_OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/bin/${SIMULATOR_OUTPUT_DIRECTORY}")

add_subdirectory(${CMAKE_SOURCE_DIR}/src/internals)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/simulator)

################################################################################
# Getting all the sub-project that contain code for prefetchers and other      #
# modular components of the simulator.                                         #
################################################################################

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l1i_no)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l1d_next_line)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l1d_ip_stride)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l1d_ipcp)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l1d_berti)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l1d_berti_iso)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l1d_no)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l2c_no)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l2c_next_line)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l2c_spp)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/l2c_spp_ppf)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/llc_no)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/sdc_no)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/prefetchers/sdc_next_line)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/l1d_lru)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/l1i_lru)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/l2c_lru)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/llc_srrip)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/llc_drrip)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/llc_lru)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/llc_topt)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/sdc_lru)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/sdc_random)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/replacements/sdc_srrip)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/tools/topt_tracer)
