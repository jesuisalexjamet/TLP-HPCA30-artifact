set(CHAMPSIM_CPU_NUMBER_CORE "1" CACHE STRING "The number of core used by the simulator.")
set(CHAMPSIM_CPU_FREQUENCY "4000" CACHE STRING "The frequency of CPU cores (measured in MHz).")
set(CHAMPSIM_CPU_DRAM_IO_FREQUENCY "800" CACHE STRING "The frequency of the IO interface between the CPU and the DRAM (measure in MHz).")
set(CHAMPSIM_CPU_PAGE_SIZE "4096" CACHE STRING "The size of the memory pages used by the CPU (measured in bytes).")

set(ENABLE_FSP OFF CACHE BOOL "Is the FSP component enabled?")
set(ENABLE_DELAYED_FSP OFF CACHE BOOL "Is the FSP component delaying prediction consumption to the L1D?")
set(ENABLE_BIMODAL_FSP OFF CACHE BOOL "Is the FSP intended to behave following a bimodal policy?")
set(ENABLE_SSP OFF CACHE BOOL "Is teh SSP component enabled?")

set(LEGACY_TRACE OFF CACHE BOOL "Enable legacy trace format.")

# Processing a bunch of sanity checks on the different variables.
if (NOT ${CHAMPSIM_CPU_NUMBER_CORE} GREATER 0)
  message (FATAL_ERROR "The number of CPU cores provided is not valid.")
endif ()

add_definitions(-DCHAMPSIM_CPU_NUMBER_CORE=${CHAMPSIM_CPU_NUMBER_CORE})
# add_definitions(-DCHAMPSIM_CPU_FREQUENCY=${CHAMPSIM_CPU_FREQUENCY})
add_definitions(-DCHAMPSIM_CPU_DRAM_IO_FREQUENCY=${CHAMPSIM_CPU_DRAM_IO_FREQUENCY})
# add_definitions(-DCHAMPSIM_CPU_PAGE_SIZE=${CHAMPSIM_CPU_PAGE_SIZE})

if (${ENABLE_FSP})
  add_definitions(-DENABLE_FSP=${ENABLE_FSP})
endif ()

if (${ENABLE_DELAYED_FSP})
  if (NOT ${ENABLE_FSP})
    message (FATAL_ERROR "The ENABLE_DELAYED_FSP flag cannot be set to ON if ENABLE_FLAG flag is set to OFF.")
  endif ()

  add_definitions(-DENABLE_DELAYED_FSP)
endif ()

if (${ENABLE_BIMODAL_FSP})
  if (NOT ${ENABLE_FSP}) # If the FSP is not enabled in the first, this doesn't make sense.
    message (FATAL_ERROR "The ENABLE_BIMODAL_FSP flag cannot be set to ON if ENABLE_FLAG flag is set to OFF.")
  endif ()

  add_definitions(-DENABLE_BIMODAL_FSP)
endif ()

if (${ENABLE_DELAYED_FSP} AND ${ENABLE_BIMODAL_FSP})
  message(FATAL_ERROR "The ENABLE_DELAYED_FSP and ENABLE_BIMODAL_FSP flags can not be set at the set time.")
endif ()

if (${ENABLE_SSP})
  add_definitions(-DENABLE_SSP=${ENABLE_SSP})
endif ()

if (${LEGACY_TRACE})
  add_definitions(-DLEGACY_TRACE)
endif()
