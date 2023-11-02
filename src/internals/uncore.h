#ifndef UNCORE_H
#define UNCORE_H

#include "champsim.h"
#include "cache.h"
#include "dram_controller.h"
#
#include <internals/components/cache.hh>
#include <internals/components/dram_controller.hh>
//#include "drc_controller.h"

//#define DRC_MSHR_SIZE 48

namespace cc = champsim::components;

// uncore
class UNCORE {
  public:

    // LLC
    CACHE LLC{"LLC", LLC_SET, LLC_WAY, LLC_SET*LLC_WAY, LLC_WQ_SIZE, LLC_RQ_SIZE, LLC_PQ_SIZE, LLC_MSHR_SIZE};
    cc::dram_controller* dram;
    cc::cache *llc;

    // DRAM
    MEMORY_CONTROLLER DRAM{"DRAM"};

    UNCORE();
    ~UNCORE ();
};

extern UNCORE uncore;

#endif
