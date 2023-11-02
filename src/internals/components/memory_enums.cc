#include <internals/components/memory_enums.hh>

namespace cc = champsim::components;

/**
 * @brief 
 * 
 * @param l 
 * @param r 
 * @return true 
 * @return false 
 */
bool champsim::components::location_match(const champsim::components::block_location& l,
                const champsim::components::block_location& r) {
    // This cover the case of is_in_dram.
    if (l == 0 && r == 0) return true;

    return l & r;
}

bool champsim::components::offchip_match(const champsim::components::block_location& l,
                const champsim::components::block_location& r) {
    if (l == cc::is_in_dram &&  r == cc::is_in_dram) {
        return true;
    } else if (l != cc::is_in_dram && r != cc::is_in_dram) {
        return true;
    }

    return false;
}