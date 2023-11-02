#include <internals/components/memory_system.hh>

namespace cc = champsim::components;

cc::memory_system::memory_system() : _lower_level_memory(nullptr) {}

cc::memory_system::~memory_system() {}

void cc::memory_system::add_upper_level_icache(cc::memory_system* ms) {
    this->_upper_level_icache.push_back(ms);
}

void cc::memory_system::set_upper_level_icache(
    const std::vector<cc::memory_system*>& uli) {
    this->_upper_level_icache = uli;
}

void cc::memory_system::add_upper_level_dcache(cc::memory_system* ms) {
    this->_upper_level_dcache.push_back(ms);
}

void cc::memory_system::set_upper_level_dcache(
    const std::vector<cc::memory_system*>& uld) {
    this->_upper_level_dcache = uld;
}

void cc::memory_system::add_upper_level_irreg_cache(cc::memory_system* ms) {
    this->_upper_level_irreg.push_back(ms);
}

void cc::memory_system::set_upper_level_irreg_cache(
    const std::vector<cc::memory_system*>& irreg) {
    this->_upper_level_irreg = irreg;
}

void cc::memory_system::set_lower_level_memory(cc::memory_system* ms) {
    this->_lower_level_memory = ms;
}

void cc::memory_system::set_dram(MEMORY_CONTROLLER* dram) {
    this->_dram = dram;
}

/**
 * @brief Returns the list of all the lower levels in the cache hierarchy.
 *
 * @return std::list<cc::memory_system *> A list of all the lower level caches
 * in the hierarchy.
 */
std::list<cc::memory_system*> cc::memory_system::lower_levels() const {
    std::list<cc::memory_system*> res;
    cc::memory_system* ms = this->_lower_level_memory;

    while (ms != nullptr) {
        res.push_back(ms);

        ms = ms->_lower_level_memory;
    }

    return res;
}
