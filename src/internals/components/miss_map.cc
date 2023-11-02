#include <cassert>
#
#include <string_view>
#
#include <internals/champsim.h>

#include <internals/simulator.hh>
#
#include <internals/ooo_cpu.h>
#include <internals/uncore.h>

#include <internals/components/miss_map.hh>

namespace cc = champsim::components;

static std::map<cc::block_location,
                std::map<cc::cache_event, cc::block_location>>
    transition_map = {
        {cc::is_in_dram,
         {{cc::evict_l2c, cc::is_in_dram},
          {cc::evict_llc, cc::is_in_dram},
          {cc::insert_l2c, cc::is_in_l2c},
          {cc::insert_llc, cc::is_in_llc}}},
        {cc::is_in_llc,
         {{cc::evict_l2c, cc::is_in_llc},
          {cc::evict_llc, cc::is_in_dram},
          {cc::insert_l2c, cc::is_in_both},
          {cc::insert_llc, cc::is_in_llc}}},
        {cc::is_in_l2c,
         {{cc::evict_l2c, cc::is_in_dram},
          {cc::evict_llc, cc::is_in_l2c},
          {cc::insert_l2c, cc::is_in_l2c},
          {cc::insert_llc, cc::is_in_both}}},
        {cc::is_in_both,
         {{cc::evict_l2c, cc::is_in_llc},
          {cc::evict_llc, cc::is_in_l2c},
          {cc::insert_l2c, cc::is_in_both},
          {cc::insert_llc, cc::is_in_both}}},
};

static std::ofstream prediction_metrics("prediction_stats.csv",
                                        std::ios::trunc | std::ios::out);

static unsigned int mm(unsigned int x, unsigned int m[]) {
    unsigned int r = 0;

    for (int i = 0; i < 11; i++) {
        r <<= 1;
        unsigned int d = x & m[i];
        r |= __builtin_parity(d);
    }

    return r;
}

static unsigned int le11[] = {0x37f, 0x431, 0x71d, 0x25c, 0x719, 0x4d5,
                              0x4b6, 0x2ca, 0x26d, 0x64f, 0x46d};

cc::metadata_cache::metadata_cache(const std::size_t& sets,
                                   const std::size_t& ways,
                                   const std::size_t& length)
    : _sim(champsim::simulator::instance()),
      _sets(sets),
      _ways(ways),
      _length(length),
      _entries(sets, miss_map_set(ways, entry(length))),
      _l2c_tag_sampler(TAG_SAMPLER_SET,
                       tag_sampler_set(16, tag_sampler_entry())),
      _llc_transactions_enabled(true),
      _miss_rate_threshold(0.0f),
      _access_cnt(0),
      _miss_cnt(0) {
    // Initializing replacement states.
    for (std::size_t i = 0; i < sets; i++) {
        for (std::size_t j = 0; j < ways; j++) {
            this->_entries[i][j].lru = ways - 1;
        }
    }

    // Initializing replacement state of the tag sampler.
    for (std::size_t i = 0; i < TAG_SAMPLER_SET; i++) {
        for (std::size_t j = 0; j < 16; j++) {
            this->_l2c_tag_sampler[i][j].lru = 15;
        }
    }

    this->_pbp = pc_based_predictor(5, 1024);

    // this->_metrics_file.open("prediction_stats.csv",
    //                          std::ios::app | std::ios::out);
}

cc::metadata_cache::metadata_cache(const metadata_cache& o) {
    this->_cpu = o._cpu;

    this->_sets = o._sets;
    this->_ways = o._ways;
    this->_length = o._length;
    this->_entries = o._entries;

    this->_pbp = o._pbp;

    this->_l2c_tag_sampler = o._l2c_tag_sampler;

    // Copying information about the LLC transaction mechanism.
    this->_llc_transactions_enabled = o._llc_transactions_enabled;
    this->_miss_rate_threshold = o._miss_rate_threshold;
    this->_access_cnt = o._access_cnt;
    this->_miss_cnt = o._miss_cnt;
}

cc::metadata_cache::metadata_cache(metadata_cache&& o) {
    this->_cpu = std::move(o._cpu);

    this->_sets = std::move(o._sets);
    this->_ways = std::move(o._ways);
    this->_length = std::move(o._length);
    this->_entries = std::move(o._entries);

    this->_pbp = std::move(o._pbp);

    this->_l2c_tag_sampler = std::move(o._l2c_tag_sampler);

    // Moving information about the LLC transaction mechanism.
    this->_llc_transactions_enabled = std::move(o._llc_transactions_enabled);
    this->_miss_rate_threshold = std::move(o._miss_rate_threshold);
    this->_access_cnt = std::move(o._access_cnt);
    this->_miss_cnt = std::move(o._miss_cnt);
}

cc::metadata_cache& cc::metadata_cache::operator=(const cc::metadata_cache& o) {
    this->_cpu = o._cpu;

    this->_sim = o._sim;
    this->_sets = o._sets;
    this->_ways = o._ways;
    this->_length = o._length;
    this->_entries = o._entries;

    this->_pbp = o._pbp;

    this->_l2c_tag_sampler = o._l2c_tag_sampler;

    // Copying information about the LLC transaction mechanism.
    this->_llc_transactions_enabled = o._llc_transactions_enabled;
    this->_miss_rate_threshold = o._miss_rate_threshold;
    this->_access_cnt = o._access_cnt;
    this->_miss_cnt = o._miss_cnt;

    return *this;
}

cc::metadata_cache& cc::metadata_cache::operator=(cc::metadata_cache&& o) {
    this->_cpu = std::move(o._cpu);

    this->_sim = std::move(o._sim);
    this->_sets = std::move(o._sets);
    this->_ways = std::move(o._ways);
    this->_length = std::move(o._length);
    this->_entries = std::move(o._entries);

    this->_pbp = std::move(o._pbp);

    this->_l2c_tag_sampler = std::move(o._l2c_tag_sampler);

    // Moving information about the LLC transaction mechanism.
    this->_llc_transactions_enabled = std::move(o._llc_transactions_enabled);
    this->_miss_rate_threshold = std::move(o._miss_rate_threshold);
    this->_access_cnt = std::move(o._access_cnt);
    this->_miss_cnt = std::move(o._miss_cnt);

    return *this;
}

cc::metadata_cache::miss_map_metrics& cc::metadata_cache::metrics() {
    return this->_metrics;
}

const cc::metadata_cache::miss_map_metrics& cc::metadata_cache::metrics()
    const {
    return this->_metrics;
}

void cc::metadata_cache::set_cpu(O3_CPU* cpu_ptr) { this->_cpu = cpu_ptr; }

void cc::metadata_cache::set_miss_rate_thrshold(const float& o) {
    this->_miss_rate_threshold = o;
}

uint64_t cc::metadata_cache::get_offset(uint64_t paddr) const {
    // paddr >>= LOG2_BLOCK_SIZE;

    return paddr % BLOCK_SIZE;
}

uint64_t cc::metadata_cache::get_set(uint64_t paddr) const {
    // paddr >>= LOG2_BLOCK_SIZE;

    return (paddr / BLOCK_SIZE) % this->_sets;
}

uint64_t cc::metadata_cache::get_tag(uint64_t paddr) const {
    // paddr >>= LOG2_BLOCK_SIZE;

    return ((paddr / BLOCK_SIZE) / this->_sets);
}

cc::metadata_cache::popular_level_detector& cc::metadata_cache::pld() {
    return this->_pld;
}

const cc::metadata_cache::popular_level_detector& cc::metadata_cache::pld()
    const {
    return this->_pld;
}

cc::metadata_cache::pc_based_predictor& cc::metadata_cache::pbp() {
    return this->_pbp;
}

const cc::metadata_cache::pc_based_predictor& cc::metadata_cache::pbp() const {
    return this->_pbp;
}

void cc::metadata_cache::mark_inserted(const uint64_t& paddr) {
    uint64_t locmap_addr = (1ULL << 48) + (paddr >> 14);
    uint64_t offset = this->get_offset(locmap_addr),
             set = this->get_set(locmap_addr), tag = this->get_tag(locmap_addr);
    miss_map_set::iterator it = this->find_entry(set, tag);

    // If there's such an entry in the miss map.
    if (it != this->_entries[set].end()) {
        // it->bit_vector[offset] = true;

        // Setting the location of block located at the address paddr.
        it->set_location(paddr, cc::is_in_l2c);

        this->update_replacement_state(set, it);
    } else {
        miss_map_set::iterator it_victim = this->find_victim(set);
        this->allocate_entry(tag, it_victim);
        this->update_replacement_state(set, it_victim);

        // it_victim->bit_vector[offset] = true;

        // Setting the location of block located at the address paddr.
        it_victim->set_location(paddr, cc::is_in_l2c);

        /*
         * TODO: On a MetaData Cache, we should send a request to the DRAM.
         *
         * However, to implement this properly we need to make sure that the
         * Metadata Cache can consume data coming from the DRAM and account for
         * the latency. Technically speaking this means: (1) Adding support for
         * MSHRs in the MetaData Cache; (2) Adding support for actual data in
         * the MetaData Cache entries; (3) Building a fill path from the DRAM to
         * the MetaData Cache.
         */
        // PACKET metadata_packet;

        // metadata_packet.fill_level = cc::cache::fill_metadata;
        // metadata_packet.full_addr = ((1ULL << 48) + (paddr >> 14));

        // uncore.DRAM.add_rq (&metadata_packet);
    }
}

void cc::metadata_cache::mark_evicted(const uint64_t& paddr) {
    uint64_t locmap_addr = (1ULL << 48) + (paddr >> 14);
    uint64_t offset = this->get_offset(locmap_addr),
             set = this->get_set(locmap_addr), tag = this->get_tag(locmap_addr);
    miss_map_set::iterator it = this->find_entry(set, tag);

    // If there's such an entry in the miss map.
    if (it != this->_entries[set].end()) {
        // assert (it->bit_vector[offset] == true);

        // it->bit_vector[offset] = false;

        // Setting the location of block located at the address paddr.
        it->set_location(paddr, cc::is_in_dram);

        this->update_replacement_state(set, it);
    }
    // else {
    //     miss_map_set::iterator it_victim = this->find_victim (set);
    //     this->allocate_entry (tag, it_victim);
    //     this->update_replacement_state (set, it_victim);
    // }
}

/**
 * @brief Update the MetaData cache accordingly to the content of the provided
 * descriptor.
 *
 * @param desc Descriptor that contains information about the way to update the
 * MetaData cache.
 */
void cc::metadata_cache::update_metadata(
    const cc::metadata_access_descriptor& desc) {
    // Computing the LocMap addres and other things to lookup the MetaData
    // Cache.
    cc::location_map& locmap = this->_sim->modeled_cpu(desc.cpu)->_location_map;
    // uint64_t ins_locmap_addr = (1ULL << 48) + (desc.paddr >> 14);
    uint64_t ins_locmap_addr = locmap.make_locmap_addr(desc.paddr);
    uint64_t ins_offset = this->get_offset(ins_locmap_addr),
             ins_set = this->get_set(ins_locmap_addr),
             ins_tag = this->get_tag(ins_locmap_addr);
    // uint64_t ev_locmap_addr = (1ULL << 48) + (desc.victim_addr >> 14);
    uint64_t ev_locmap_addr = locmap.make_locmap_addr(desc.victim_addr);
    uint64_t ev_offset = this->get_offset(ev_locmap_addr),
             ev_set = this->get_set(ev_locmap_addr),
             ev_tag = this->get_tag(ev_locmap_addr);

    // First, we lookup the cache for the inserted block.
    miss_map_set::iterator it = this->find_entry(ins_set, ins_tag);

    // On prefetch fills that result in a MetaData Cache miss, we do not do
    // anything.
    if (desc.access_type == cc::cache::prefetch &&
        it == this->_entries[ins_set].end())
        return;

    // Accounting for an access to the MetaData Cache.
    this->_access_cnt++;

    if (it != this->_entries[ins_set].end()) {  // MetaData Cache hit.
        cc::cache_event e;
        cc::block_location old_loc = it->location(desc.paddr), new_loc,
                           actual_location;

        switch (desc.type) {
            case cc::is_l2c:
                e = cc::insert_l2c;
                break;
            case cc::is_llc:
                e = cc::insert_llc;
                break;

            default:
                break;
        }

        new_loc = cc::metadata_cache::_update_location(old_loc, e);

        /*
         * When compiling in debug mode we run further checks to see
         * when doest the predictor diverges from the actual location.
         */
        actual_location = this->_pld.predict_perfect(desc.paddr);

        try {
            // If the predicted location is different from the actual location,
            // we have a problem..
            if (actual_location != new_loc) {
                throw std::runtime_error(
                    "The predicted location and the actual location of the "
                    "cache "
                    "block are different.");
            }
        } catch (std::runtime_error&
                     e) {  // We catch the exception to correct the error.
#ifndef NDEBUG
            // std::clog << "[MISS MAP - ERROR CORRECTION]: " << e.what()
            //   << std::endl;
#endif  // NDEBUG

            // new_loc = actual_location;
        }

        // Now we can update the location accordingly.
        it->set_location(desc.paddr, new_loc);
        it->dirty = true;
        it->heuristic_based = false;

        this->update_replacement_state(ins_set, it);
    } else {  // MetaData Cache miss.
        // this->_handle_insertion_miss(desc);

        // Account for a miss in the MetaData Cache.
        this->_miss_cnt++;
    }

    // If the victim block was invalid, we do not proceed.
    if (desc.victim_addr == 0ULL) return;

    // Second, we need to the cache for the evicted block.
    it = this->find_entry(ev_set, ev_tag);

    this->_access_cnt++;

    if (it != this->_entries[ev_set].end()) {  // MetaData Cache hit.
        cc::cache_event e;
        cc::block_location old_loc = it->location(desc.victim_addr), new_loc,
                           actual_location;

        switch (desc.type) {
            case cc::is_l2c:
                e = cc::evict_l2c;
                break;
            case cc::is_llc:
                e = cc::evict_llc;
                break;

            default:
                break;
        }

        new_loc = cc::metadata_cache::_update_location(old_loc, e);

        /*
         * When compiling in debug mode we run further checks to see
         * when doest the predictor diverges from the actual location.
         */
        actual_location = this->_pld.predict_perfect(desc.victim_addr);

        try {
            // If the predicted location is different from the actual location,
            // we have a problem..
            if (actual_location != new_loc) {
                throw std::runtime_error(
                    "The predicted location and the actual location of the "
                    "cache "
                    "block are different.");
            }
        } catch (std::runtime_error&
                     e) {  // We catch the exception to correct the error.
#ifndef NDEBUG
            // std::clog << "[MISS MAP - ERROR CORRECTION]: " << e.what()
            //           << std::endl;
#endif  // NDEBUG

            // new_loc = actual_location;
        }

        // Now we can update the location accordingly.
        it->set_location(desc.victim_addr, new_loc);
        it->dirty = true;
        it->heuristic_based = false;

        this->update_replacement_state(ev_set, it);
    } else {  // MetaData Cache miss.
        // this->_handle_eviction_miss(desc);

        this->_miss_cnt++;
    }
}

void cc::metadata_cache::update_llc_transaction_mechanism() {
    float miss_rate = 0.0;

    // If the access counter goes back to zero, then we take a decision.
    if (this->_access_cnt >= 1000000) {
        miss_rate = static_cast<float>(this->_miss_cnt) /
                    static_cast<float>(this->_access_cnt);

        this->_llc_transactions_enabled =
            (miss_rate <= this->_miss_rate_threshold);

        // std::cout << miss_rate << " " << this->_miss_rate_threshold << " "
        //           << this->_llc_transactions_enabled << std::endl;

        this->_miss_cnt = 0;
        this->_access_cnt = 0;
    }
}

bool cc::metadata_cache::is_miss(const uint64_t& paddr) {
    // Computing the LocMap addres and other things to lookup the MetaData
    // Cache.
    uint64_t locmap_addr = (1ULL << 48) + (paddr >> 14);
    uint64_t offset = this->get_offset(locmap_addr),
             set = this->get_set(locmap_addr), tag = this->get_tag(locmap_addr);
    miss_map_set::iterator it = this->find_entry(set, tag);

    if (it != this->_entries[set].end()) {
        return it->location(paddr) == cc::is_in_dram;
    } else {  // In this case, we go for the most aggressive decision.
        return false;
    }
}

cc::block_location cc::metadata_cache::predict(const uint64_t& paddr) {
    uint64_t locmap_addr = (1ULL << 48) + (paddr >> 14),
             set = this->get_set(locmap_addr), tag = this->get_tag(locmap_addr);
    miss_map_set::iterator it = this->find_entry(set, tag);

    if (it != this->_entries[set].end()) {
        return it->location(paddr);
    } else {
        return this->_pld.predict();
    }
}

cc::locmap_prediction_descriptor cc::metadata_cache::predict_desc(
    PACKET& packet, const uint64_t& pc, const uint64_t& paddr,
    const uint64_t& lq_index) {
    LSQ_ENTRY& lq_entry = this->_cpu->LQ.entry[lq_index];
    bool was_correct = false, onchip_pred = !lq_entry.went_offchip_pred,
         lp_onchip_pred = false;

    const cc::location_map& locmap = this->_cpu->_location_map;
    uint64_t locmap_addr = locmap.make_locmap_addr(paddr),
             set = this->get_set(locmap_addr), tag = this->get_tag(locmap_addr);
    miss_map_set::iterator it;
    cc::locmap_prediction_descriptor desc = {.metadata_cache_hit = true,
                                             .location = cc::is_in_l2c,
                                             .destinations = {cc::is_in_l2c}};
    cc::block_location perfect_location = this->_pld.predict_perfect(paddr),
                       predicted_location =
                           this->_pbp.predict_desc(pc, lq_entry);
    prediction_descriptor pred_qos;

    predicted_location =
        this->_pbp.predict(lq_entry) ? cc::is_in_dram : cc::is_in_l2c;
    ;
    lp_onchip_pred = (predicted_location == cc::is_in_l2c);

    if (lq_entry.went_offchip_pred) this->_metrics.aggreement_checks++;

    // if ((!lp_onchip_pred && lq_entry.went_offchip_pred) ||
    //     lq_entry.perc_feature->perceptron_weights_sum >=
    //         this->_pbp._high_conf_threshold) {
    if (lq_entry.went_offchip_pred) {
        // if (lp_onchip_pred) {
        desc.destinations = {cc::is_in_l2c, cc::is_in_dram};
        desc.metadata_cache_hit = false;

        this->_metrics.agreements++;
    }

    // Marking the LQ entry as reuslting in an L1D miss.
    lq_entry.l1d_miss_offchip_pred = !lp_onchip_pred;
    lq_entry.l1d_offchip_pred_used = true;

    ITERATE_SET(merged, packet.lq_index_depend_on_me, LQ_SIZE) {
        this->_cpu->LQ.entry[merged].l1d_miss_offchip_pred = !lp_onchip_pred;
        this->_cpu->LQ.entry[merged].l1d_offchip_pred_used = true;
    }

    // stats.
    was_correct = cc::offchip_match(perfect_location, predicted_location);

    if (onchip_pred) {
        if (was_correct)
            this->_metrics.per_pc_corrects[pc].first++;
        else
            this->_metrics.per_pc_corrects[pc].second++;
    }

    return desc;
}

cc::locmap_prediction_descriptor cc::metadata_cache::predict_desc_on_prefetch(
    PACKET& packet, const uint64_t& pc, const uint64_t& paddr,
    const uint64_t& lq_index) {
    LSQ_ENTRY& lq_entry = this->_cpu->LQ.entry[lq_index];
    bool was_correct = false, pf_offchip_pred = false;

    const cc::location_map& locmap = this->_cpu->_location_map;
    uint64_t locmap_addr = locmap.make_locmap_addr(paddr),
             set = this->get_set(locmap_addr), tag = this->get_tag(locmap_addr);
    miss_map_set::iterator it;
    cc::locmap_prediction_descriptor desc = {.metadata_cache_hit = true,
                                             .location = cc::is_in_l2c,
                                             .destinations = {cc::is_in_l2c}};
    cc::block_location perfect_location = this->_pld.predict_perfect(paddr);

    packet.pf_went_offchip_pred = this->_cpu->offchip_pred->predict_on_prefetch(packet);

    if (packet.pf_went_offchip_pred) {
        desc.location = cc::is_in_dram;
        desc.destinations = {cc::is_in_l2c, cc::is_in_dram};
        desc.metadata_cache_hit = false;
    }

    return desc;
}

void cc::metadata_cache::update_replacement_state(const uint64_t& set,
                                                  miss_map_set::iterator it) {
    uint8_t old_lru = it->lru;

    for (miss_map_set::iterator it_loop = this->_entries[set].begin();
         it_loop != this->_entries[set].end(); it_loop++) {
        if (it_loop->lru < old_lru) {
            it_loop->lru++;
        }
    }

    it->lru = 0;
}

cc::metadata_cache::miss_map_set::iterator cc::metadata_cache::find_victim(
    const uint64_t& set) {
    miss_map_set::iterator it =
        std::find_if(this->_entries[set].begin(), this->_entries[set].end(),
                     [&ways = this->_ways](const entry& e) -> bool {
                         return (e.lru == ways - 1);
                     });

    // Sanity check.
    assert(it != this->_entries[set].end());

    return it;
}

void cc::metadata_cache::allocate_entry(const uint64_t& tag,
                                        miss_map_set::iterator it) {
    it->tag = tag;
    it->valid = true;
    it->dirty = false;
    it->heuristic_based =
        false;  // For now, the entry was not based on a heuristic.

    // for (std::size_t i = 0; i < it->bit_vector.size (); i++) {
    //     it->bit_vector[i] = false;
    // }

    // We need to clear the locations.
    std::memset(it->data, 0x0, BLOCK_SIZE * sizeof(uint8_t));
}

cc::metadata_cache::miss_map_set::iterator cc::metadata_cache::find_entry(
    const uint64_t& set, const uint64_t& tag) {
    return std::find(this->_entries[set].begin(), this->_entries[set].end(),
                     tag);
}

/**
 * @brief Determines whether a L2C set must be sampled in the L2C tag sampler.
 *
 * @param set_idx The set index in the L2C.
 * @return true The L2C set must be sampled.
 * @return false The L2C set must not be sampled.
 */
bool cc::metadata_cache::is_l2c_set_sampled(const uint64_t& set_idx) const {
    // return true;
    return (set_idx % (1024 / TAG_SAMPLER_SET) == 0);
}

bool cc::metadata_cache::lookup_l2c_tag_sampler(const uint64_t& paddr) const {
    uint64_t set_idx = (paddr >> LOG2_BLOCK_SIZE) % 0x400;
    uint64_t sampler_set_idx = set_idx / (1024 / TAG_SAMPLER_SET);
    // uint64_t sampler_set_idx = set_idx / TAG_SAMPLER_SET;
    uint64_t tag = (paddr >> (LOG2_BLOCK_SIZE + 0xa));

    tag_sampler_set::const_iterator it = std::find_if(
        this->_l2c_tag_sampler[sampler_set_idx].cbegin(),
        this->_l2c_tag_sampler[sampler_set_idx].cend(),
        [tag](const tag_sampler_entry& e) -> bool { return e.tag == tag; });

    return it != this->_l2c_tag_sampler[sampler_set_idx].cend();
}

void cc::metadata_cache::update_l2c_tag_sampler(const uint64_t& paddr) {
    uint64_t set_idx = (paddr >> LOG2_BLOCK_SIZE) % 0x400;
    uint64_t sampler_set_idx = set_idx / (1024 / TAG_SAMPLER_SET);
    // uint64_t sampler_set_idx = set_idx / TAG_SAMPLER_SET;
    uint64_t tag = (paddr >> (LOG2_BLOCK_SIZE + 0xa));

    // std::clog << sampler_set_idx << std::endl;

    tag_sampler_set::iterator it = std::find_if(
        this->_l2c_tag_sampler[sampler_set_idx].begin(),
        this->_l2c_tag_sampler[sampler_set_idx].end(),
        [tag](const tag_sampler_entry& e) -> bool { return e.tag == tag; });

    if (it != this->_l2c_tag_sampler[sampler_set_idx].end()) {  // Hit.
        // Here, we simply update the LRU states.
        std::for_each(this->_l2c_tag_sampler[sampler_set_idx].begin(),
                      this->_l2c_tag_sampler[sampler_set_idx].end(),
                      [old_lru = it->lru](tag_sampler_entry& e) -> void {
                          if (e.lru < old_lru) e.lru++;
                      });

        // The hit entry goes to MRU.
        it->lru = 0;
    } else {  // Miss.
        // Looking for a victim.
        tag_sampler_set::iterator it_victim = std::find_if(
            this->_l2c_tag_sampler[sampler_set_idx].begin(),
            this->_l2c_tag_sampler[sampler_set_idx].end(),
            [](const tag_sampler_entry& e) -> bool { return e.lru == 15; });

        std::for_each(this->_l2c_tag_sampler[sampler_set_idx].begin(),
                      this->_l2c_tag_sampler[sampler_set_idx].end(),
                      [old_lru = it_victim->lru](tag_sampler_entry& e) -> void {
                          if (e.lru < old_lru) e.lru++;
                      });

        it_victim->lru = 0;
        it_victim->tag = tag;
    }
}

/**
 * @brief
 *
 * @param packet
 */
void cc::metadata_cache::check_correctness(const PACKET& packet) {
    // The packet is given a route to follow, we check whether a hit happened in
    // the predicted location.
    static std::map<cc::sdc_routes, cc::cache_type> match_map = {
        {cc::l1d_dram, cc::is_dram},
        {cc::l1d_llc, cc::is_llc},
        {cc::sdc_l2c_dram, cc::is_l2c},
    };

    // Sanity check.
    assert(packet.served_from != cc::is_l1d);

    // this->_metrics.predicted_routes[packet.route][packet.served_from]++;

    // if (match_map.at(packet.route) == packet.served_from) {  // Correct...
    //     this->_metrics.correct++;
    // } else {  // Incorrect...
    //     this->_metrics.incorrect++;
    // }
}

/**
 * @brief
 *
 * @param packet
 */
void cc::metadata_cache::return_data(PACKET& packet) {
    O3_CPU* curr_cpu = this->_sim->modeled_cpu(packet.cpu);
    cc::location_map& locmap = curr_cpu->_location_map;
    miss_map_set::iterator it_victim;
    uint64_t set = this->get_set(packet.full_addr),
             tag = this->get_tag(packet.full_addr);

    // A writeback reply doesn't bring any valuable information for us. Just
    // skip it.
    if (packet.type == WRITEBACK) return;

    // Once we receive a packet back from the LLC,
    // we elect a victim and replace it.
    it_victim = this->find_victim(set);

    // We need to trigger a writeback.
    if (it_victim->valid && it_victim->dirty && !it_victim->heuristic_based) {
        PACKET miss_packet;
        uint64_t victim_locmap_addr =
            locmap.make_locmap_addr(it_victim->tag, set);

        miss_packet.ip = 0ULL;
        miss_packet.is_data = 0;
        miss_packet.is_metadata = true;
        miss_packet.metadata_insertion = false;
        // miss_packet.metadata_type = desc.type;
        miss_packet.cpu = packet.cpu;
        miss_packet.fill_level = cc::cache::fill_metadata;
        miss_packet.address = (victim_locmap_addr >> LOG2_BLOCK_SIZE);
        miss_packet.full_addr = victim_locmap_addr;
        miss_packet.type = static_cast<uint32_t>(cc::cache::writeback);
        miss_packet.event_cycle = curr_cpu->current_core_cycle();

        // uncore.llc->add_write_queue(miss_packet);

        // Here, we writeback the information contained in the victim block to
        // the LocMap.
        // std::memcpy(locmap[victim_locmap_addr].data, it_victim->data,
        //             BLOCK_SIZE);
    }

    // We can now allocate the entry of the Catalog Cache.
    this->allocate_entry(tag, it_victim);

    // Computing latency.
    this->_metrics.latency +=
        (curr_cpu->current_core_cycle() - packet.event_cycle);

    // WIP: If the pointer is invalid (nullptr) we allocate a memory block.
    try {
        // Retrieving the LocMap entry.
        // std::memcpy(it_victim->data, locmap[packet.full_addr].data,
        // BLOCK_SIZE);

        // Printing the content of the LocMap entry.
        // std::cout << std::string_view((const char*) it_victim->data,
        // BLOCK_SIZE) << std::endl;

        // Measuring the avg. (median?) position in the cache hierarchy.
        for (std::size_t i = 0; i < BLOCK_SIZE; i++) {
            for (std::size_t j = 0; j < 4; j++) {
                this->_metrics.avgs_loc[static_cast<cc::block_location>(
                    (it_victim->data[i] >> (2 * j)) & 0x3)];
            }
        }
    } catch (...) {
        // NOP.
    }

    cc::cache_event e;
    cc::block_location old_loc = it_victim->location(packet.full_addr), new_loc,
                       actual_location;

    switch (packet.metadata_type) {
        case cc::is_l2c:
            e = (packet.metadata_insertion ? cc::insert_l2c : cc::evict_l2c);
            break;
        case cc::is_llc:
            e = (packet.metadata_insertion ? cc::insert_llc : cc::evict_llc);
            break;

        default:
            break;
    }

    new_loc = cc::metadata_cache::_update_location(old_loc, e);

    try {
        // If the predicted location is different from the actual location,
        // we have a problem..
        if (actual_location != new_loc) {
            throw std::runtime_error(
                "The predicted location and the actual location of the "
                "cache "
                "block are different.");
        }
    } catch (std::runtime_error&
                 e) {  // We catch the exception to correct the error.
#ifndef NDEBUG
        // std::clog << "[MISS MAP - ERROR CORRECTION]: " << e.what() <<
        // std::endl;
#endif  // NDEBUG

        // new_loc = actual_location;
    }

    // Setting the location of block located at the address paddr.
    it_victim->set_location(packet.full_addr, new_loc);
    it_victim->dirty = true;

    // Updating the replacement state.
    this->update_replacement_state(set, it_victim);
}

void cc::metadata_cache::_handle_insertion_miss(
    const metadata_access_descriptor& desc) {
    PACKET miss_packet;
    O3_CPU* curr_cpu = this->_sim->modeled_cpu(desc.cpu);
    cc::location_map& locmap = curr_cpu->_location_map;
    uint64_t locmap_addr = locmap.make_locmap_addr(desc.paddr), set = 0ULL,
             tag = 0ULL;
    miss_map_set::iterator it_victim;

    miss_packet.ip = 0ULL;
    miss_packet.is_data = 0;
    miss_packet.is_metadata = true;
    miss_packet.metadata_insertion = true;
    miss_packet.metadata_type = desc.type;
    miss_packet.cpu = desc.cpu;
    miss_packet.fill_level = cc::cache::fill_metadata;
    miss_packet.address = (locmap_addr >> LOG2_BLOCK_SIZE);
    miss_packet.full_addr = locmap_addr;
    miss_packet.type = static_cast<uint32_t>(cc::cache::load);
    miss_packet.event_cycle = curr_cpu->current_core_cycle();

    // uncore.llc->add_read_queue(miss_packet);
    uncore.DRAM.add_rq(&miss_packet);

    // this->return_data(miss_packet);
}

void cc::metadata_cache::_handle_eviction_miss(
    const metadata_access_descriptor& desc) {
    PACKET miss_packet;
    O3_CPU* curr_cpu = this->_sim->modeled_cpu(desc.cpu);
    cc::location_map& locmap = curr_cpu->_location_map;
    uint64_t locmap_addr = locmap.make_locmap_addr(desc.victim_addr),
             set = 0ULL, tag = 0ULL;
    miss_map_set::iterator it_victim;

    miss_packet.ip = 0ULL;
    miss_packet.is_data = 0;
    miss_packet.is_metadata = true;
    miss_packet.metadata_insertion = false;
    miss_packet.metadata_type = desc.type;
    miss_packet.cpu = desc.cpu;
    miss_packet.fill_level = cc::cache::fill_metadata;
    miss_packet.address = (locmap_addr >> LOG2_BLOCK_SIZE);
    miss_packet.full_addr = locmap_addr;
    miss_packet.type = static_cast<uint32_t>(cc::cache::load);
    miss_packet.event_cycle = curr_cpu->current_core_cycle();

    // uncore.llc->add_read_queue(miss_packet);
    uncore.DRAM.add_rq(&miss_packet);

    // this->return_data(miss_packet);
}

bool cc::metadata_cache::_check_prediction_l2c(const uint64_t& addr,
                                               const uint8_t& cpu) {
    uint32_t set =
        champsim::simulator::instance()->modeled_cpu(cpu)->l2c->get_set(addr);
    uint16_t way =
        champsim::simulator::instance()->modeled_cpu(cpu)->l2c->get_way(addr,
                                                                        set);

    return (way != champsim::simulator::instance()
                       ->modeled_cpu(cpu)
                       ->l2c->associativity());
}

bool cc::metadata_cache::_check_prediction_llc(const uint64_t& addr) {
    uint32_t set = uncore.llc->get_set(addr);
    uint16_t way = uncore.llc->get_way(addr, set);

    return (way != uncore.llc->associativity());
}

cc::block_location cc::metadata_cache::pc_based_predictor::predict_desc(
    const uint64_t& pc, const LSQ_ENTRY& lq_entry) {
    uint64_t sig = 0ULL;
    static std::map<cc::cache_type, cc::block_location> conv_map = {
        {cc::is_l2c, cc::is_in_l2c},
        {cc::is_llc, cc::is_in_llc},
        {cc::is_dram, cc::is_in_dram},
    };

    // Computing the signature.
    // sig = jenkins_hash(
    //           folded_xor(lq_entry.perc_feature->info->last_n_load_pc_sig, 2))
    //           %
    //       this->_size;
    sig = (pc % this->_size);

    std::map<cc::cache_type, uint32_t>::iterator it = std::max_element(
        this->_pc_counters.at(sig).begin(), this->_pc_counters.at(sig).end(),
        [](const auto& a, const auto& b) -> bool {
            return a.second < b.second;
        });

    return conv_map[it->first];
}

void cc::metadata_cache::pc_based_predictor::report_hit(
    const uint64_t& pc, const cc::cache_type& type, const LSQ_ENTRY& lq_entry,
    bool reverse) {
    // uint64_t sig = 0ULL;

    // // Computing the signature.
    // // sig = jenkins_hash(
    // //           folded_xor(lq_entry.perc_feature->info->last_n_load_pc_sig,
    // 2))
    // //           %
    // //       this->_size;
    // sig = (pc % this->_size);

    // std::map<cc::cache_type, uint32_t>& entry = this->_pc_counters.at(sig);

    // if (!reverse) {
    //     for (auto& [first, second] : entry) {
    //         // WIP: Let's reduce the number of bits used for the
    //         // counters.
    //         if (first == type) {
    //             if (second < this->_counters_max) second++;
    //             // if (second < UINT8_MAX) second++;
    //         } else {
    //             if (second > 0U) second--;
    //         }
    //     }
    // } else {
    //     for (auto& [first, second] : entry) {
    //         // WIP: Let's reduce the number of bits used for the
    //         // counters.
    //         if (first == type) {
    //             if (second > 0U) second--;
    //             // if (second < UINT8_MAX) second++;
    //         } else {
    //             if (second < this->_counters_max) second++;
    //         }
    //     }
    // }
}

void cc::metadata_cache::pc_based_predictor::check_and_update_act_thresh(
    const LSQ_ENTRY& lq_entry) {
    // bool on_limit = false;
    // if (lq_entry.went_offchip) {
    //     this->_psel = std::min(2047, this->_psel + 1);
    // } else {
    //     this->_psel = std::max(-2048, this->_psel - 1);
    // }

    // if (this->_psel == 2047) {
    //     this->_threshold = std::max(-64, this->_threshold - 1);
    //     on_limit = true;
    // } else if (this->_psel == -2048) {
    //     this->_threshold = std::min(63, this->_threshold + 1);
    //     on_limit = true;
    // }

    // // std::cout << this->_psel << std::endl;

    // if (on_limit) {
    //     // std::cout << this->_threshold << std::endl;
    //     this->_psel = 0;
    // }
}

void cc::metadata_cache::pc_based_predictor::train(const LSQ_ENTRY& lq_entry) {
    bool pred_output = lq_entry.l1d_miss_offchip_pred,
         true_output = lq_entry.went_offchip;
    // uint64_t pa_sig = lq_entry.virtual_address >> LOG2_PAGE_SIZE,
    //          pc_sig = lq_entry.ip,
    //          n_load_pcs_sig =
    //          lq_entry.perc_feature->info->last_n_load_pc_sig, n_vpns_sig =
    //          lq_entry.perc_feature->info->last_n_vpn_sig;
    // pc_sig = jenkins_hash(pc_sig) % this->_f1.size();
    // pa_sig = jenkins_hash(pa_sig) % this->_f2.size();
    // n_load_pcs_sig =
    //     jenkins_hash(n_load_pcs_sig) % this->_f3.size();
    // n_vpns_sig = jenkins_hash(n_vpns_sig) % this->_f4.size();
    uint64_t idx1 = perceptron_predictor::_process_pc_offset(
                 lq_entry.perc_feature->info, 1024),
             idx2 = perceptron_predictor::_process_pc_first_access(
                 lq_entry.perc_feature->info, 1024),
             idx3 = perceptron_predictor::_process_offset_first_access(
                 lq_entry.perc_feature->info, 1024),
             idx4 = perceptron_predictor::_process_pc_cl_offset(
                 lq_entry.perc_feature->info, 1024),
             idx5 = perceptron_predictor::_process_last_n_loads_pcs(
                 lq_entry.perc_feature->info, 1024);
    // uint64_t idx1 = this->compute_pc_14_43_11_false(lq_entry),
    //          idx2 = this->compute_pc_1_53_10_false(lq_entry),
    //          idx3 = this->compute_pc_3_11_16_true(lq_entry),
    //          idx4 = this->compute_pc_6_20_0_true(lq_entry),
    //          idx5 = this->compute_pc_6_20_14_true(lq_entry),
    //          idx6 = this->compute_pc_8_16_5_false(lq_entry),
    //          idx7 = this->compute_offset_0_6_true(lq_entry),
    //          idx8 = this->compute_offset_1_6_true(lq_entry);

    // We only train if the LQ entry was marked as resulting in a L1D miss.
    if (!lq_entry.l1d_offchip_pred_used) return;

    if (true_output) {
        // if (pred_output != true_output) {
        if (this->_f1[idx1] < this->_counters_max) this->_f1[idx1]++;
        if (this->_f2[idx2] < this->_counters_max) this->_f2[idx2]++;
        if (this->_f3[idx3] < this->_counters_max) this->_f3[idx3]++;
        if (this->_f4[idx4] < this->_counters_max) this->_f4[idx4]++;
        if (this->_f5[idx5] < this->_counters_max) this->_f5[idx5]++;
        // if (this->_f6[idx6] < this->_counters_max) this->_f6[idx6]++;
        // if (this->_f7[idx7] < this->_counters_max) this->_f7[idx7]++;
        // if (this->_f8[idx6] < this->_counters_max) this->_f8[idx8]++;
        // }
    } else {
        // if (pred_output != true_output) {
        if (this->_f1[idx1] > this->_counters_min) this->_f1[idx1]--;
        if (this->_f2[idx2] > this->_counters_min) this->_f2[idx2]--;
        if (this->_f3[idx3] > this->_counters_min) this->_f3[idx3]--;
        if (this->_f4[idx4] > this->_counters_min) this->_f4[idx4]--;
        if (this->_f5[idx5] > this->_counters_min) this->_f5[idx5]--;
        // if (this->_f6[idx6] > this->_counters_min) this->_f6[idx6]--;
        // if (this->_f7[idx7] > this->_counters_min) this->_f7[idx7]--;
        // if (this->_f8[idx8] > this->_counters_min) this->_f8[idx8]--;
        // }
    }

    // stats
    if (lq_entry.went_offchip_pred != lq_entry.went_offchip) {
        if (lq_entry.l1d_miss_offchip_pred && lq_entry.went_offchip)
            this->true_pos++;
        else if (lq_entry.l1d_miss_offchip_pred && !lq_entry.went_offchip)
            this->false_pos++;
        else if (!lq_entry.l1d_miss_offchip_pred && lq_entry.went_offchip)
            this->false_neg++;
        else if (!lq_entry.l1d_miss_offchip_pred && !lq_entry.went_offchip)
            this->true_neg++;
    }

    // activation threshold update?
    this->train_count++;
}

bool cc::metadata_cache::pc_based_predictor::predict(LSQ_ENTRY& lq_entry) {
    int32_t sum = 0;

    uint64_t idx1 = perceptron_predictor::_process_pc_offset(
                 lq_entry.perc_feature->info, 1024),
             idx2 = perceptron_predictor::_process_pc_first_access(
                 lq_entry.perc_feature->info, 1024),
             idx3 = perceptron_predictor::_process_offset_first_access(
                 lq_entry.perc_feature->info, 1024),
             idx4 = perceptron_predictor::_process_pc_cl_offset(
                 lq_entry.perc_feature->info, 1024),
             idx5 = perceptron_predictor::_process_last_n_loads_pcs(
                 lq_entry.perc_feature->info, 1024);
    // uint64_t idx1 = this->compute_pc_14_43_11_false(lq_entry),
    //          idx2 = this->compute_pc_1_53_10_false(lq_entry),
    //          idx3 = this->compute_pc_3_11_16_true(lq_entry),
    //          idx4 = this->compute_pc_6_20_0_true(lq_entry),
    //          idx5 = this->compute_pc_6_20_14_true(lq_entry),
    //          idx6 = this->compute_pc_8_16_5_false(lq_entry),
    //          idx7 = this->compute_offset_0_6_true(lq_entry),
    //          idx8 = this->compute_offset_1_6_true(lq_entry);

    // Updating the history of PCs.
    if (this->_pc_history.size() >= 16) this->_pc_history.pop_back();
    this->_pc_history.push_front(lq_entry.ip);

    sum = this->_f1[idx1] + this->_f2[idx2] + this->_f3[idx3] +
          this->_f4[idx4] + this->_f5[idx5]
        //   + this->_f6[idx6]
        //   + this->_f7[idx7] + this->_f8[idx8]
        ;

    return (sum >= this->_threshold);
}

bool cc::metadata_cache::pc_based_predictor::predict_on_prefetch(
    const PACKET& packet) {
}

void cc::metadata_cache::pc_based_predictor::clear_stats() {
    this->true_pos = 0;
    this->false_pos = 0;
    this->false_neg = 0;
    this->true_neg = 0;
    this->train_count = 0;

    this->l1d_hits_on_offchip_pred = 0;
    this->l2c_hits_on_offchip_pred = 0;
    this->llc_hits_on_offchip_pred = 0;
}

void cc::metadata_cache::pc_based_predictor::dump_stats() {
    std::cout << "PP stats" << std::endl
              << "perc_true_pos " << this->true_pos << std::endl
              << "perc_false_pos " << this->false_pos << std::endl
              << "perc_false_neg " << this->false_neg << std::endl
              << "perc_true_neg " << this->true_neg << std::endl
              << std::endl
              << "Hermes request avoided upon L1D hits: "
              << this->l1d_hits_on_offchip_pred << std::endl
              << "Hermes request avoided upon L2C hits: "
              << this->l2c_hits_on_offchip_pred << std::endl
              << "Hermes request avoided upon LLC hits: "
              << this->llc_hits_on_offchip_pred << std::endl
              << std::endl;
}

uint64_t cc::metadata_cache::pc_based_predictor::compute_pc_1_53_10_false(
    const LSQ_ENTRY& lq_entry) {
    uint64_t val = this->_pc_history[9], mask = ((1ULL << 52) - 1ULL);

    val >>= 1;
    val &= mask;

    val = jenkins_hash(val);

    return val % 1024;
}

uint64_t cc::metadata_cache::pc_based_predictor::compute_pc_3_11_16_true(
    const LSQ_ENTRY& lq_entry) {
    uint64_t val = this->_pc_history[15], mask = ((1ULL << 8) - 1ULL);

    val >>= 3;
    val &= mask;
    val ^= lq_entry.ip;

    val = jenkins_hash(val);

    return val % 1024;
}

uint64_t cc::metadata_cache::pc_based_predictor::compute_pc_8_16_5_false(
    const LSQ_ENTRY& lq_entry) {
    uint64_t val = this->_pc_history[4], mask = ((1ULL << 8) - 1ULL);

    val >>= 8;
    val &= mask;

    val = jenkins_hash(val);

    return val % 1024;
}

uint64_t cc::metadata_cache::pc_based_predictor::compute_pc_6_20_0_true(
    const LSQ_ENTRY& lq_entry) {
    uint64_t val = lq_entry.ip, mask = ((1ULL << 14) - 1ULL);

    val >>= 6;
    val &= mask;
    val ^= lq_entry.ip;

    val = jenkins_hash(val);

    return val % 1024;
}

uint64_t cc::metadata_cache::pc_based_predictor::compute_pc_6_20_14_true(
    const LSQ_ENTRY& lq_entry) {
    uint64_t val = this->_pc_history[13], mask = ((1ULL << 14) - 1ULL);

    val >>= 6;
    val &= mask;
    val ^= lq_entry.ip;

    val = jenkins_hash(val);

    return val % 1024;
}

uint64_t cc::metadata_cache::pc_based_predictor::compute_pc_14_43_11_false(
    const LSQ_ENTRY& lq_entry) {
    uint64_t val = this->_pc_history[10], mask = ((1ULL << 29) - 1ULL);

    val >>= 14;
    val &= mask;

    val = jenkins_hash(val);

    return val % 1024;
}

uint64_t cc::metadata_cache::pc_based_predictor::compute_offset_0_6_true(
    const LSQ_ENTRY& lq_entry) {
    uint64_t val = lq_entry.physical_address & 0x3f;

    val ^= lq_entry.ip;

    val = jenkins_hash(val);
    return val % 1024;
}

uint64_t cc::metadata_cache::pc_based_predictor::compute_offset_1_6_true(
    const LSQ_ENTRY& lq_entry) {
    uint64_t val = lq_entry.physical_address & 0x3f;

    val >>= 1;
    val ^= lq_entry.ip;

    val = jenkins_hash(val);
    return val % 1024;
}

/**
 * @brief Helper function that updates the location of a block based on its
 * previous location and an event.
 *
 * @param loc Previous location of the memory block.
 * @param e Event that occured on this memory block.
 * @return cc::block_location The new location of the block in the memory
 * hierarchy.
 */
cc::block_location cc::metadata_cache::_update_location(
    const cc::block_location& loc, const cc::cache_event& e) {
    // TODO: We should ensure the validity of a transition.
    return transition_map[loc][e];
}

cc::location_map::location_map()
    : _block_coverage(8ULL * BLOCK_SIZE / 2ULL),
      _block_coverage_size(BLOCK_SIZE * this->_block_coverage),
      _block_count((DRAM_SIZE << 10ULL) /
                   (this->_block_coverage_size >> 10ULL)),
      _base_addr(1ULL << 48),
      _map_counter(0ULL),
      _entries(this->_block_count, cc::metadata_cache::entry(0ULL)) {
    // Allocating a flat array of size BLOCK_COUNT.
    this->_locmap_data = new uint8_t[BLOCK_SIZE * this->_block_count];
}

cc::location_map::~location_map() {
    // Freeing memory.
    delete[] this->_locmap_data;
}

const uint64_t& cc::location_map::base_addr() const { return this->_base_addr; }

// /**
//  * @brief Given an address in the location map, returns a pointer to the
//  * matching entry.
//  *
//  * @param locmap_addr The address in the location map.
//  * @return uint8_t* Pointer to the location map entry.
//  */
// uint8_t* cc::location_map::operator[](uint64_t locmap_addr) {
//     uint8_t* locmap_data = nullptr;
//     uint64_t offset = 0ULL, entry_id = 0ULL;
//     static uint64_t map_counter = 0ULL;
//     std::map<uint64_t, uint64_t>::iterator entry_it;

//     // Getting rid of the offset bit of the location map address.
//     entry_id = locmap_addr >> LOG2_BLOCK_SIZE;

//     if ((entry_it = this->_matching_map.find(entry_id)) ==
//         this->_matching_map.end()) {
//         this->_matching_map[entry_id] = map_counter++;

//         locmap_data = &this->_locmap_data[BLOCK_SIZE * map_counter];
//     } else {
//         locmap_data = &this->_locmap_data[BLOCK_SIZE * entry_it->second];
//     }

//     return locmap_data;
// }

cc::metadata_cache::entry& cc::location_map::operator[](
    const uint64_t& locmap_addr) {
    uint64_t entry_id = 0ULL;
    std::map<uint64_t, uint64_t>::iterator entry_it;

    entry_id = locmap_addr >> LOG2_BLOCK_SIZE;

    if ((entry_it = this->_matching_map.find(entry_id)) ==
        this->_matching_map.end()) {
        this->_matching_map[entry_id] = this->_map_counter++;

#ifndef NDEBUG
        if (std::find(
                this->_entries[this->_map_counter - 1].visited_tags.begin(),
                this->_entries[this->_map_counter - 1].visited_tags.end(),
                entry_id) ==
            this->_entries[this->_map_counter - 1].visited_tags.end()) {
            this->_entries[this->_map_counter - 1].visited_tags.push_back(
                entry_id);
        }

        assert(this->_entries[this->_map_counter - 1].visited_tags.size() == 1);
#endif  // NDEBUG

        return this->_entries[this->_map_counter - 1];
    } else {
#ifndef NDEBUG
        if (std::find(this->_entries[entry_it->second].visited_tags.begin(),
                      this->_entries[entry_it->second].visited_tags.end(),
                      entry_id) ==
            this->_entries[entry_it->second].visited_tags.end()) {
            this->_entries[entry_it->second].visited_tags.push_back(entry_id);
        }

        assert(this->_entries[entry_it->second].visited_tags.size() == 1);
#endif  // NDEBUG

        return this->_entries[entry_it->second];
    }
}

/**
 * @brief
 *
 * @param paddr
 * @return uint64_t
 */
uint64_t cc::location_map::make_locmap_addr(const uint64_t& paddr) const {
    return (this->_base_addr) | ((paddr >> 0xe) << 0x6);
}

/**
 * @brief
 *
 * @param tag
 * @param set
 * @return uint64_t
 */
uint64_t cc::location_map::make_locmap_addr(const uint64_t& tag,
                                            const uint64_t& set) const {
    uint64_t locmap_addr = this->_base_addr;

    locmap_addr |= tag << (LOG2_BLOCK_SIZE + 0x4);
    locmap_addr |= set << LOG2_BLOCK_SIZE;

    // Sanity checks. The offset bits must be null.
    assert((locmap_addr & 0x3f) == 0ULL);

    return locmap_addr;
}

std::ostream& operator<<(std::ostream& os, const cc::metadata_cache& mm) {
    os << std::endl << mm.metrics();

    return os;
}

std::ostream& operator<<(std::ostream& os,
                         const cc::metadata_cache::miss_map_metrics& mmm) {
    static std::map<cc::block_location, std::string> cache_type_str_map = {
        {cc::is_in_l2c, "L2C"},
        {cc::is_in_llc, "LLC"},
        {cc::is_in_both, "L2C + LLC"},
        {cc::is_in_dram, "DRAM"},
    };

    float miss_rate = static_cast<float>(mmm.misses);
    float avg_miss_latency = static_cast<float>(mmm.latency) /
                             static_cast<float>(mmm.llc_locmap_miss);
    // float avg_loc_at_ins =
    //     std::accumulate(
    //         mmm.avgs_loc.begin(), mmm.avgs_loc.end(), 0.0f,
    //         [](const float& a, const float& b) -> float { return a + b; }) /
    //     mmm.avgs_loc.size();

    miss_rate /=
        (static_cast<float>(mmm.misses) + static_cast<float>(mmm.hits));

    os << "[MISS MAP] HITS MISSES" << std::endl
       << mmm.hits << " " << mmm.misses << " Miss ratio: " << miss_rate
       << std::endl
       << std::endl;

    os << "Correct " << mmm.correct << " Incorrect " << mmm.incorrect
       << std::endl;
    os << "PLD Correct " << mmm.pld_correct << " Incorrect "
       << mmm.pld_incorrect << std::endl
       << std::endl;
    os << "Avverage miss latency: " << avg_miss_latency << " "
       << mmm.llc_locmap_miss << std::endl
       << std::endl;
    os << "L2C PREFETCHES REACHED DRAM: " << mmm.l2c_pref_reached_dram
       << std::endl;
    os << "HERMES/PP AGREEMENTS: " << mmm.aggreement_checks << " "
       << mmm.agreements << std::endl;

    for (const auto& [first, second] : mmm.predicted_routes) {
        for (const auto& [perfect_location, count] : second) {
            os << cache_type_str_map.at(first) << ": "
               << cache_type_str_map.at(perfect_location) << " " << count
               << std::endl;
        }

        os << std::endl;
    }

    for (const auto& [pc, p] : mmm.per_pc_corrects) {
        float acc = (float)p.first;
        acc /= (acc + (float)p.second);

        os << std::hex << pc << std::dec << " " << p.first << " " << p.second
           << " " << acc << std::endl;
    }

    os << std::endl;

    return os;
}

std::ofstream& operator<<(
    std::ofstream& ofs,
    const champsim::components::metadata_cache::prediction_descriptor& desc) {
    ofs << std::hex << desc.pc << std::dec << " " << std::hex << desc.claddr
        << std::dec << " " << std::hex << desc.pfn << std::dec << " "
        << desc.correct << " " << desc.cycle << " " << desc.l2c_counter << " "
        << desc.llc_counter << " " << desc.dram_counter << " "
        << desc.paddr_feature_cnt;

    return ofs;
}
