#include <iostream>
#include <iomanip>
#
#include <bitset>
#include <vector>
#include <map>
#
#include "cache.h"

using namespace champsim::instrumentations;

// initialize replacement state
void CACHE::llc_initialize_replacement()
{

}

// find replacement victim
uint32_t CACHE::llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
    // Getting the victim block.
    uint32_t victim_idx = lru_victim(cpu, instr_id, set, current_set, ip, full_addr, type);

    if (this->cache_type != IS_L1I) {
		if (all_warmup_complete) {
			this->usages.counters ()[this->usages.bitmap ({set, victim_idx}).count ()]++;
		}

		this->usages.bitmap ({set, victim_idx}).reset ();
	}

    // baseline LRU
    return victim_idx;
}

// called on every cache hit and cache fill
void CACHE::llc_update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint32_t size, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    // Prior to any further processing we track accesses to cache blocks' words.
	uint64_t offset = (full_addr & usage_tracker::offset_mask);

    this->usages.set ({set, way, offset}, size);

    // baseline LRU
    if (hit && (type == WRITEBACK)) // writeback hit does not update LRU state
        return;

    return lru_update(set, way);
}

void CACHE::llc_replacement_final_stats()
{
    std::cout << this->NAME << " block usage stats" << std::endl;

	for (const std::pair<uint8_t, uint64_t>& e : this->usages.counters ()) {
		std::cout << std::setw (4) << std::left << static_cast<uint32_t> (e.first) << e.second << std::endl;
	}

	std::cout << std::endl;
}
