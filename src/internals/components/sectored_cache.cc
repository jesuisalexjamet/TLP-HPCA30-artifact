#include <exception>
#include <iostream>
#
#include <internals/dram_controller.h>
#include <internals/ooo_cpu.h>
#include <internals/uncore.h>
#
#include <internals/simulator.hh>
#
#include <internals/prefetchers/iprefetcher.hh>
#include <internals/replacements/ireplacementpolicy.hh>
#
#include <internals/components/sectored_cache.hh>

#define maxRRPV 3

#define CHAMPSIM_RECORD_L1D_MISSES_LATENCY
#undef CHAMPSIM_RECORD_L1D_MISSES_LATENCY

#if defined(CHAMPSIM_RECORD_L1D_MISSES_LATENCY)
static std::ofstream l1d_misses("l1d_misses.csv",
                                std::ios::out | std::ios::trunc);
#endif  // CHAMPSIM_RECORD_L1D_MISSES_LATENCY

static std::map<cc::sdc_routes, std::string> routes_map = {
    {cc::sdc_dram, "SDC->DRAM"},
    {cc::sdc_l2c_dram, "SDC->L2C"},
    {cc::sdc_llc_dram, "SDC->LLC"},
};

using namespace champsim;
namespace cc = champsim::components;
namespace cp = champsim::prefetchers;

static auto mshr_predicate_generator = [](const uint8_t& block_size,
                                          const PACKET& comp_packet) {
    uint8_t log2_block_size = static_cast<uint8_t>(std::log2(block_size));

    return std::function<bool(const PACKET&)>(
        [log2_block_size, comp_packet](const PACKET& e) -> bool {
            return ((e.full_addr >> log2_block_size) ==
                    (comp_packet.full_addr >> log2_block_size));
        });
};

static std::map<uint16_t, uint64_t> recency_hit_pos;

cc::sectored_cache::sectored_cache() {}

cc::sectored_cache::sectored_cache(const uint64_t& cpu_idx) {
    this->_cpu = cpu_idx;
}

cc::sectored_cache::~sectored_cache() {}

cc::sectored_cache::cache_layout cc::sectored_cache::layout() const {
    return cc::sectored_cache::sectored_cache_layout;
}

std::size_t cc::sectored_cache::sets() const { return this->_set_degree; }

std::size_t cc::sectored_cache::associativity() const {
    return this->_associativity_degree;
}

void cc::sectored_cache::report(std::ostream& os, const size_t& i) {
    // std::ofstream edges_usages_file (this->_report_filename, std::ios::out |
    // std::ios::app); reuse_tracker::accuracy_metrics overall_metrics;
    //
    // for (const auto& e: this->_trackers) {
    // 	for (const auto& f: e.metrics ().reuse_heat_map) {
    // 		overall_metrics.reuse_heat_map[f.first] += f.second;
    // 	}
    // }
    //
    // for (const auto& e: overall_metrics.reuse_heat_map) {
    // 	edges_usages_file << e.first << ";" << e.second << std::endl;
    // }

    if (this->check_type(cc::is_llc)) {
    cc:
        cache::report(os, 0);
    } else {
        cc::cache::report(os, i);
    }

    // If this is an SDC we report stats of the routing engine.
    if (this->check_type(cc::is_sdc)) {
        const cc::routing_engine::predictor_metrics& metrics =
            this->_re.metrics();
        float accurate = metrics.accurate, inaccurate = metrics.inaccurate,
              accuracy = 0.0f;
        float total_pred = 0.0f, curr_prop = 0.0f;

        accuracy = accurate / (accurate + inaccurate);

        os << "Routing engine accuracy: " << accuracy << std::endl;

        for (const auto& [first, second] : metrics.accurate_predictions) {
            uint64_t total_prediction = second.first + second.second;
            accurate = second.first;
            inaccurate = second.second;

            accuracy = accurate / (accurate + inaccurate);

            os << routes_map[first] << " " << total_prediction << " "
               << accuracy << std::endl;
        }

        // Computing optimal paths.
        // for (const auto& [first, second]: metrics.optimal_predictions) {
        //     total_pred += static_cast<float> (second);
        // }
        //
        // for (const auto& [first, second]: metrics.optimal_predictions) {
        //     curr_prop = static_cast<float> (second) / total_pred;
        //
        //     os << routes_map[first] << " " << second << " " << curr_prop <<
        //     std::endl;
        // }
        // for (const auto& [first, second]: metrics.optimal_predictions) {
        // 	uint64_t total_prediction = second.first + second.second;
        // 	accurate = second.first;
        // 	inaccurate = second.second;
        //
        // 	accuracy = accurate / (accurate + inaccurate);
        //
        // 	os << routes_map[first] << " " << total_prediction << " " <<
        // accuracy << std::endl;
        // }

        os << std::endl;

        os << "Prediction changes: " << std::endl;

        for (const auto& [first, second] : metrics._prediction_changes) {
            for (const auto& [first_, second_] : second) {
                os << routes_map[first] << " to " << routes_map[first_] << " "
                   << second_ << std::endl;
            }
        }

        os << std::endl;

        os << "Cache pollution stats: " << std::endl;

        for (const auto& [first, second] : this->_block_usages) {
            os << first << " blocks used " << second << " times" << std::endl;
        }

        os << std::endl;
    }
}

void cc::sectored_cache::reset_stats() {
    cc::cache::reset_stats();

    this->_lmp.metrics() = cc::load_miss_predictor::lmp_stats();

    // If this is a SDC, we reset the SDC.
    if (this->check_type(cc::is_sdc)) {
        this->_re.reset();
        this->_re.metrics().clear();

        for (auto it = this->_block_usages.begin();
             it != this->_block_usages.end(); it++) {
            it->second = 0;
        }
    }
}

const std::string& cc::sectored_cache::report_filename() const {
    return this->_report_filename;
}

void cc::sectored_cache::update_footprint(const uint64_t& addr,
                                          const footprint_bitmap& footprint) {
    uint16_t way;
    uint32_t set = this->get_set(addr);

    // The asosciated block is not in the cache so there is nothing more to do.
    if ((way = this->get_way(addr, set)) == this->_associativity_degree) {
        return;
    }

    // OR-ing the footprints.
    this->_footprints[set][way] |= footprint;
}

uint16_t cc::sectored_cache::get_way(const uint64_t& addr,
                                     const uint32_t& set) const {
    uint16_t way = this->_associativity_degree;
    tag_type tag = this->_get_tag(addr);

    for (std::size_t i = 0; i < this->_associativity_degree; i++) {
        if (this->_tags[set][i] == tag && this->_is_sector_valid(set, i)) {
            return i;
        }
    }

    return way;
}

uint32_t cc::sectored_cache::get_set(const uint64_t& addr) const {
    uint64_t mask = ((1ULL << static_cast<uint64_t>(
                          std::log2(this->_set_degree))) -
                     1ULL),
             displacement =
                 static_cast<uint64_t>(std::log2(this->_sector_size));

    return static_cast<uint32_t>((addr >> displacement) & mask);
}

uint64_t cc::sectored_cache::get_paddr(const uint32_t& set,
                                       const uint16_t& way) const {
    return this->_blocks[set][way].full_addr;
}

uint32_t cc::sectored_cache::get_block(const uint64_t& addr) const {
    uint64_t mask = ((1ULL << static_cast<uint64_t>(
                          std::log2(this->_sectoring_degree))) -
                     1ULL),
             displacement = static_cast<uint64_t>(std::log2(this->_block_size));

    return static_cast<uint32_t>((addr >> displacement) & mask);
}

bool cc::sectored_cache::is_valid(const uint32_t& set, const uint16_t& way) {
    return this->_is_sector_valid(set, way);
}

void cc::sectored_cache::invalidate_line(const uint64_t& full_addr) {
    uint32_t set = this->get_set(full_addr);
    uint16_t way = this->get_way(full_addr, set);

    // The cache line was not found (basically equivalent to a miss).
    if (way == this->_associativity_degree) {
        return;
    }

    this->_fill_valid_bits(set, way, false);
}

void cc::sectored_cache::_init_cache_impl(const pt::ptree& props) {
    std::size_t sector_size, set_size, total_size;

    // Calling the original version of this method before extending it.
    cc::cache::_init_cache_impl(props);

    this->_set_degree = props.get<std::size_t>("set_degree");
    this->_associativity_degree =
        props.get<std::size_t>("associativity_degree");
    this->_sectoring_degree = props.get<std::size_t>("sectoring_degree");
    this->_block_size = props.get<std::size_t>("block_size");

    // this->_report_filename = getenv ("REPORT_FILENAME");
    // this->_report_filename = "reuses/" + this->_name +
    // this->_report_filename;

    // Computing footprint metrics.
    sector_size = this->_block_size * this->_sectoring_degree;
    set_size = sector_size * this->_associativity_degree;
    total_size = set_size * this->_set_degree;

    this->_sector_size = sector_size;
    this->_offset_mask = sector_size - 1ULL;

    std::cout << "Creating a sectored-cache: \"" << this->_name << "\" "
              << "with " << this->_block_size << " B/block, "
              << this->_associativity_degree << " sectors per set, "
              << this->_set_degree << " sets "
              << " and total size of " << total_size << " bytes." << std::endl;

    // The data array._mshr.retire_
    this->_blocks = data_array(
        this->_set_degree,
        data_set_array(this->_associativity_degree * this->_sectoring_degree,
                       BLOCK()));

    // The tag array is made up of one tag per sector.
    this->_tags = tag_array(this->_set_degree,
                            tag_set_array(this->_associativity_degree, 0x0));

    // Both the valid & dirty bits are made up of an entrt per block in the
    // cache.
    this->_valid_bits = valid_array(
        this->_set_degree,
        valid_set_array(this->_associativity_degree,
                        valid_bits_type(this->_sectoring_degree, 0UL)));
    this->_dirty_bits = valid_array(
        this->_set_degree,
        dirty_set_array(this->_associativity_degree,
                        valid_bits_type(this->_sectoring_degree, 0UL)));

    // There's only one replacement state per cache sector.
    this->_repl = replacement_state_array(
        this->_set_degree,
        replacement_state_set_array(this->_associativity_degree,
                                    this->_associativity_degree - 1));
    this->_repl_srrip = replacement_state_array(
        this->_set_degree,
        replacement_state_set_array(this->_associativity_degree, maxRRPV));

    // There's a single footprint bitmap per cache sector.
    this->_footprints = footprint_bitmap_array(
        this->_set_degree, footprint_bitmap_set_array(
                               this->_associativity_degree,
                               footprint_bitmap(this->_sectoring_degree, 0UL)));

    for (std::size_t i = 1; i < this->_sectoring_degree + 1; i++) {
        this->_block_usages[i] = 0;
    }

    // Initializing the reuse trackers.
    if (this->_cache_type == cc::is_l1d) {
        // this->_trackers = std::vector<reuse_tracker *> (this->_set_degree,
        // new reuse_tracker (this->_associativity_degree, this));

        // 1024 entries for the PC-indexed first level table. 256 entires for
        // the hit-miss history indexed table.
        this->_lmp = cc::load_miss_predictor(1024, 256);
    }
}

cc::sectored_cache::tag_type cc::sectored_cache::_get_tag(
    const uint64_t& addr) const {
    uint64_t displacement = static_cast<uint64_t>(std::log2(
        this->_block_size * this->_sectoring_degree * this->_set_degree));

    return static_cast<uint64_t>(addr >> displacement);
}

cc::sectored_cache::tag_type cc::sectored_cache::_get_tag(
    const PACKET& packet) const {
    return this->_get_tag(packet.full_addr);
}

bool cc::sectored_cache::_should_slice_packet(const PACKET& packet) const {
    uint64_t start_addr = packet.full_addr,
             end_addr = packet.full_addr + packet.memory_size - 1ULL;

    if (start_addr == 0ULL) {
        return false;
    }

    return this->get_set(start_addr) != this->get_set(end_addr);
}

std::vector<PACKET> cc::sectored_cache::_slice_packet(
    const PACKET& packet) const {
    PACKET p1 = packet,
           p2 = packet;  // Initializing p1 and p2 as copies of packet.

    // Correcting full address on p2.
    p2.full_addr = (p2.full_addr & ~this->_offset_mask) + this->_sector_size;

    // Computing memory sizes.
    p1.memory_size = (p2.full_addr - 1ULL) - p1.full_addr;
    p2.memory_size = packet.memory_size - p1.memory_size;

    p1.is_sliced = true;
    p1.is_first_slice = true;
    p2.is_sliced = true;
    p2.is_first_slice = false;

    return {p1, p2};
}

bool cc::sectored_cache::_is_hit(const PACKET& packet) const {
    return this->_is_hit(packet, this->get_set(packet.full_addr));
}

bool cc::sectored_cache::_is_hit(const PACKET& packet,
                                 const uint32_t& set) const {
    return (this->get_way(packet.full_addr, set) !=
            this->_associativity_degree);
}

cc::hit_types cc::sectored_cache::__is_hit(const PACKET& packet) const {
    return this->__is_hit(packet, this->get_set(packet.full_addr));
}

cc::hit_types cc::sectored_cache::__is_hit(const PACKET& packet,
                                           const uint32_t& set) const {
    return (this->get_way(packet.full_addr, set) != this->_associativity_degree)
               ? cc::loc_hit
               : cc::line_miss;
}

bool cc::sectored_cache::_is_sector_valid(const uint32_t& set,
                                          const uint16_t& way) const {
    // All blocks in a sector have to have a valid bit set to 1.
    return (this->_valid_bits[set][way].count() ==
            this->_valid_bits[set][way].size());
}

/**
 * @brief Tells if any blocks in the sector pointed by this packet are valid.
 * @param packet A packet that corresponds to a memory request.
 * @pre The packet has to point at a sector that is actually in the cache.
 */
bool cc::sectored_cache::_is_sector_valid(const PACKET& packet) const {
    uint32_t set = this->get_set(packet.full_addr);
    uint16_t way = this->get_way(packet.full_addr, set);

    if (way == this->_associativity_degree) {
        throw std::logic_error(
            "Cannot check the valid bits of a missing sector.");
    }

    return this->_is_sector_valid(set, way);
}

/**
 * @brief Tells if all blocks in the sector pointed by this packet are dirty.
 * @param packet A packet that corresponds to a memory request.
 */
bool cc::sectored_cache::_is_sector_dirty(const uint32_t& set,
                                          const uint16_t& way) const {
    // At least one block in a sector have to have a dirty bit set to 1.
    return this->_dirty_bits[set][way].any();
}

/**
 * @brief Tells if all blocks in the sector pointed by this packet are dirty.
 * @param packet A packet that corresponds to a memory request.
 * @pre The packet has to point at a sector that is actually in the cache.
 */
bool cc::sectored_cache::_is_sector_dirty(const PACKET& packet) const {
    uint32_t set = this->get_set(packet.full_addr);
    uint16_t way = this->get_way(packet.full_addr, set);

    if (way == this->_associativity_degree) {
        throw std::logic_error(
            "Cannot check the dirty bits of an missing sector.");
    }

    return this->_is_sector_dirty(set, way);
}

void cc::sectored_cache::_fill_valid_bits(const uint32_t& set,
                                          const uint16_t& way,
                                          const bool& value) {
    this->_fill_valid_bits(set, way, 0, this->_sector_size, value);
}

void cc::sectored_cache::_fill_valid_bits(const uint32_t& set,
                                          const uint16_t& way,
                                          const uint16_t& start_offset,
                                          const uint32_t& size,
                                          const bool& value) {
    for (std::size_t i = start_offset, curr_idx = 0;
         i < this->_sector_size && i < (size + start_offset); i++) {
        curr_idx = i / this->_block_size;

        this->_valid_bits[set][way][curr_idx] = value;
    }
}

void cc::sectored_cache::_fill_valid_bits(const uint32_t& set,
                                          const uint16_t& way,
                                          const std::vector<bool>& valid_bits) {
    for (std::size_t i = 0; i < this->_block_size; i++) {
        this->_valid_bits[set][way][i] = valid_bits[i];
    }
}

void cc::sectored_cache::_fill_dirty_bits(const uint32_t& set,
                                          const uint16_t& way,
                                          const bool& value) {
    this->_fill_dirty_bits(set, way, 0, this->_sector_size, value);
}

void cc::sectored_cache::_fill_dirty_bits(const uint32_t& set,
                                          const uint16_t& way,
                                          const uint16_t& start_offset,
                                          const uint32_t& size,
                                          const bool& value) {
    for (std::size_t i = start_offset, curr_idx = 0;
         i < this->_sector_size && i < (size + start_offset); i++) {
        curr_idx = i / this->_block_size;

        this->_dirty_bits[set][way][curr_idx] = value;
    }
}

void cc::sectored_cache::_fill_tag_array(const uint32_t& set,
                                         const uint16_t& way,
                                         const uint64_t& tag) {
    this->_tags[set][way] = tag;
}

std::vector<PACKET>::iterator cc::sectored_cache::_add_mshr(
    const PACKET& packet) {
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);
    auto it = std::find_if(
        this->_mshr.begin(), this->_mshr.end(),
        [](const PACKET& e) -> bool { return (e.address == 0x0); });

    if (it != this->_mshr.end()) {
        *it = packet;
        it->returned = INFLIGHT;
        it->cycle_enqueued = curr_cpu->current_core_cycle();
    } else {
        throw std::logic_error("No free spot found in the MSHR.");
    }

    // Returning the iterator for a possible clearance of the entry.
    return it;
}

void cc::sectored_cache::_fill_footprint_bitmap(const uint32_t& set,
                                                const uint16_t& way,
                                                const uint16_t& start_offset,
                                                const uint32_t& size) {
    for (std::size_t i = start_offset, curr_idx = 0;
         i < this->_sector_size && i != (size + start_offset); i++) {
        curr_idx = i / this->_block_size;

        this->_footprints[set][way][curr_idx] = true;
    }
}

void cc::sectored_cache::_clear_footprint_bitmap(const uint32_t& set,
                                                 const uint16_t& way) {
    this->_footprints[set][way].reset();
}

void cc::sectored_cache::_fill_cache(const uint32_t& set, const uint16_t& way,
                                     const PACKET& packet) {
    // Sanity checks.
    if (packet.data == 0x0 &&
        (this->_cache_type == cc::is_itlb || this->_cache_type == cc::is_dtlb ||
         this->_cache_type == cc::is_stlb)) {
        throw std::logic_error("Cannot fill a TLB entry with an empty packet.");
    }

    // Working with the usage footprints. We only care about these when it comes
    // from the SDC and the sector is valid.
    if (this->check_type(cc::is_sdc) && this->_is_sector_valid(set, way)) {
        this->_block_usages[this->_footprints[set][way].count()]++;
    }

    for (std::size_t i = 0; i < this->_sectoring_degree; i++) {
        if (this->_blocks[set][this->_sectoring_degree * way + i].prefetch &&
            !this->_blocks[set][this->_sectoring_degree * way + i].used) {
            this->_pf_useless++;

            this->_pf_useless_per_loc[this->_blocks[set][this->_sectoring_degree * way + i].served_from]++;
        }

        this->_blocks[set][this->_sectoring_degree * way + i].prefetch =
            ((packet.type == static_cast<uint32_t>(cc::cache::prefetch))
                 ? true
                 : false);
        this->_blocks[set][this->_sectoring_degree * way + i].used = false;

        if (this->_blocks[set][this->_sectoring_degree * way + i].prefetch) {
            this->_pf_fill++;
        }

        this->_blocks[set][this->_sectoring_degree * way + i].delta =
            packet.delta;
        this->_blocks[set][this->_sectoring_degree * way + i].signature =
            packet.signature;
        this->_blocks[set][this->_sectoring_degree * way + i].confidence =
            packet.confidence;

        this->_blocks[set][this->_sectoring_degree * way + i].tag =
            packet.address;
        this->_blocks[set][this->_sectoring_degree * way + i].address =
            packet.address;
        this->_blocks[set][this->_sectoring_degree * way + i].full_addr =
            packet.full_addr;
        this->_blocks[set][this->_sectoring_degree * way + i].data =
            packet.data;
        this->_blocks[set][this->_sectoring_degree * way + i].ip = packet.ip;
        this->_blocks[set][this->_sectoring_degree * way + i].cpu = packet.cpu;
        this->_blocks[set][this->_sectoring_degree * way + i].instr_id =
            packet.instr_id;

        this->_blocks[set][this->_sectoring_degree * way + i].served_from =
            packet.served_from;
    }

    /*
     * If the request is served by another component than the WOC, all valid
     * bits are set. Otherwise, we just set as valid the words returned by the
     * WOC.
     */
    this->_fill_valid_bits(set, way, true);

    // if (packet.served_from != cc::is_woc) {
    // 	this->_fill_valid_bits (set, way, true);
    // } else {
    // 	this->_fill_valid_bits (set, way, packet.valid_bits);
    // }

    this->_fill_dirty_bits(set, way, false);

    // Filling the tag array.
    this->_fill_tag_array(set, way, this->_get_tag(packet.full_addr));

    // Updating the correspong footprint bitmap.
    this->_clear_footprint_bitmap(set, way);
    this->_fill_footprint_bitmap(set, way,
                                 packet.full_addr & (this->_sector_size - 1ULL),
                                 packet.memory_size);
}

void cc::sectored_cache::_initialize_replacement_state() {}

void cc::sectored_cache::_update_replacement_state(
    helpers::cache_access_descriptor& desc) {
    cc::metadata_access_descriptor ma_desc;

    // Adding the inverse page table.
    desc.inverse_table = &helper::inverse_table;

    this->_replacement_policy->update_replacement_state(desc);
}

uint32_t cc::sectored_cache::_find_victim(
    helpers::cache_access_descriptor& desc) {
    desc.inverse_table = &helper::inverse_table;

    return this->_replacement_policy->find_victim(desc);
}

void cc::sectored_cache::_final_replacement_stats() {}

/**
 * @brief
 * @pre it must be a valid iterator within the MSHR entries of this cache.
 *
 * @param it
 */
void cc::sectored_cache::_update_fill_path_lower_mshrs(mshr_iterator it) {
    // Checking pre-condition(s).
    if (it == this->_mshr.end()) {
        throw std::runtime_error("Provided invalid iterator.");
    }

    auto ll = this->lower_levels();

    for (cc::memory_system* e : ll) {
        // Converting e into a pointer to a sectored cache instance.
        if (cc::sectored_cache* sc = dynamic_cast<cc::sectored_cache*>(e);
            sc != nullptr) {
            // Looking for a similar packet in the MSHR of the sc cache.
            mshr_iterator sc_mshr_it = sc->find_mshr(*it);

            // No matching MSHR found in sc. We just skip this cache.
            if (sc_mshr_it == sc->_mshr.end()) {
                continue;
            }

            // Now we have a matching MSHR in sc, we can simply copy the fill
            // path to it.
            // sc_mshr_it->fill_path = it->fill_path;
            sc_mshr_it->merge_fill_path(*it);
        }
    }
}

void cc::sectored_cache::_update_pf_offchip_pred_lower(mshr_iterator it) {
    // Checking pre-condition(s).
    if (it == this->_mshr.end()) {
        throw std::runtime_error("Provided invalid iterator.");
    }

    auto ll = this->lower_levels();

    for (cc::memory_system* e : ll) {
        // Converting e into a pointer to a sectored cache instance.
        if (cc::sectored_cache* sc = dynamic_cast<cc::sectored_cache*>(e);
            sc != nullptr) {
            int pq_index = sc->prefetch_queue()->check_queue(&*it);

            if (pq_index != -1) {
                sc->_prefetch_queue->entry[pq_index].pf_went_offchip_pred =
                    it->pf_went_offchip_pred;
            }
        }
    }
}

/**
 * @brief Handles as many misses as possible.
 */
void cc::sectored_cache::_handle_fill() {
    bool evict_dirty = false;
    uint32_t fill_cpu, way;
    uint32_t set, block;
    O3_CPU* curr_cpu = nullptr;
    helpers::cache_access_descriptor desc, victim_desc;

    while (this->_writes_avail_cycle != 0) {
        BLOCK victim_block;
        auto it =
            std::min_element(this->_mshr.begin(), this->_mshr.end(),
                             [](const PACKET& a, const PACKET& b) -> bool {
                                 return (b.returned != COMPLETED ||
                                         (a.returned == COMPLETED &&
                                          a.event_cycle < b.event_cycle));
                             });
        PACKET& curr_packet = *it;

        fill_cpu = curr_packet.cpu;
        curr_cpu = champsim::simulator::instance()->modeled_cpu(fill_cpu);

        if (curr_packet.returned != COMPLETED || fill_cpu >= NUM_CPUS ||
            curr_packet.event_cycle > curr_cpu->current_core_cycle()) {
            return;  // TODO: Shouldn't it be a continue instead of a return?
        }

        /*
         * Let's find a victim. One should note that as we are working on a
         * sectored cache, the victim should be an entire sector and not just a
         * single block.
         */
        set = this->get_set(curr_packet.full_addr);
        block = this->get_block(curr_packet.full_addr);

        // In order to find a victim, we first need to fill the cache access
        // descriptor accordingly.
        desc.cpu = fill_cpu;
        desc.set = set;
        desc.way = UINT8_MAX;
        desc.full_addr = curr_packet.full_addr;
        desc.full_vaddr = curr_packet.full_v_addr;
        desc.victim_addr = 0ULL;
        desc.pc = curr_packet.ip;
        desc.type = curr_packet.type;
        desc.hit = false;

        way = this->_find_victim(desc);

        victim_block = this->_blocks[set][this->_sectoring_degree * way];

        // If the lower level cache is a distill cache, we transmit the
        // footprint. if (this->_lower_level_memory) { dynamic_cast<cc::cache *>
        // (this->_lower_level_memory)->update_footprint
        // (victim_block->full_addr, this->_footprints[set][way]);
        // }

        // this->_clear_footprint_bitmap (set, way);

        // TODO: This needs to be corrected as the lower level can be a DRAM if
        // this one is an LLC.
        if (this->check_type(cc::is_llc)) {
            evict_dirty = (this->_dram && this->_is_sector_dirty(set, way));

            if (evict_dirty &&
                this->_dram->get_occupancy(2, victim_block.address) ==
                    this->_dram->get_size(2, victim_block.address)) {
                return;
            }
        } else {
            evict_dirty =
                (this->_lower_level_memory && this->_is_sector_dirty(set, way));

            if (evict_dirty && this->_lower_level_memory->write_queue_occupancy(
                                   victim_block.address) ==
                                   this->_lower_level_memory->write_queue_size(
                                       victim_block.address)) {
                // Original ChampSim code calls for a give up here, but that's
                // actually not needed.
                return;
            }
        }

        // If we are evicting a dirty block from the cache, we have to write it
        // back to the lower-level.
        if (evict_dirty &&
            !(this->check_type(cc::is_llc) || this->check_type(cc::is_sdc))) {
            PACKET writeback_packet;

            writeback_packet.fill_level = next_fill_level(
                static_cast<cc::cache::fill_levels>(this->_fill_level));
            writeback_packet.cpu = fill_cpu;
            writeback_packet.address = victim_block.address;
            writeback_packet.full_addr =
                victim_block.full_addr & ~this->_offset_mask;
            writeback_packet.memory_size = this->_sector_size;
            writeback_packet.data = victim_block.data;
            writeback_packet.instr_id = curr_packet.instr_id;
            writeback_packet.ip =
                0;  // Apparently, writebacks to not have any IP.
            writeback_packet.type = static_cast<uint32_t>(cc::cache::writeback);
            writeback_packet.event_cycle = curr_cpu->current_core_cycle();

            this->_lower_level_memory->add_write_queue(writeback_packet);
        } else if (evict_dirty && this->check_type(cc::is_llc)) {
            PACKET writeback_packet;

            writeback_packet.fill_level = next_fill_level(
                static_cast<cc::cache::fill_levels>(this->_fill_level));
            writeback_packet.cpu = fill_cpu;
            writeback_packet.address = victim_block.address;
            writeback_packet.full_addr =
                victim_block.full_addr & ~this->_offset_mask;
            writeback_packet.memory_size = this->_sector_size;
            writeback_packet.data = victim_block.data;
            writeback_packet.instr_id = curr_packet.instr_id;
            writeback_packet.ip =
                0;  // Apparently, writebacks to not have any IP.
            writeback_packet.type = static_cast<uint32_t>(cc::cache::writeback);
            writeback_packet.event_cycle = curr_cpu->current_core_cycle();

            this->_dram->add_wq(&writeback_packet);
        }

        if (curr_packet.is_metadata) {
            curr_cpu->_mm.metrics().llc_locmap_miss++;
        }

        this->_fill_cache(set, way, curr_packet);

        // #ifndef NDEBUG  // Sanity check. We check if we created a duplicate
        // in the
        //                 // cache.
        //         uint32_t lookup_way;

        //         for (lookup_way = 0; lookup_way <
        //         this->_associativity_degree;
        //              lookup_way++) {
        //             if (this->_tags[set][lookup_way] ==
        //             this->_get_tag(curr_packet) &&
        //                 this->_is_sector_valid(set, lookup_way)) {
        //                 // If we have a different matching entry, we have a
        //                 problem. break;
        //             }
        //         }

        //         if (lookup_way != way) {
        //             throw std::runtime_error("Introducing duplicates in the
        //             cache.");
        //         }
        // #endif  // NDEBUG

        // Sanity check, if that assertion fails it means that we had a
        // duplicate.
        // assert(this->get_way(victim_block.full_addr, set) ==
        //        this->_associativity_degree);

        if (curr_packet.type == static_cast<uint32_t>(cc::cache::rfo) &&
            this->_cache_type == cc::is_l1d) {
            this->_fill_dirty_bits(set, way,
                                   curr_packet.full_addr & this->_offset_mask,
                                   curr_packet.memory_size, true);
        }

        if (curr_packet.cycle_enqueued > 0) {
            this->_total_miss_latency +=
                (curr_cpu->current_core_cycle() - curr_packet.cycle_enqueued);
        }

#if defined(CHAMPSIM_RECORD_L1D_MISSES_LATENCY)
        if (this->check_type(cc::is_l1d) && curr_packet.type == LOAD) {
            uint64_t delta_cycle =
                (curr_cpu->current_core_cycle() - curr_packet.cycle_enqueued);

            l1d_misses << curr_cpu->LQ.entry[curr_packet.lq_index].went_offchip
                       << ";" << delta_cycle << std::endl;
        }
#endif  // CHAMPSIM_RECORD_L1D_MISSES_LATENCY

        // Update prefetcher.
        victim_desc = desc;

        victim_desc.victim_addr = victim_block.full_addr;

        switch (this->_cache_type) {
            case cc::is_l1i:
                // this->_l1i_prefetcher_cache_operate ();
                break;

            case cc::is_l1d:
                this->_prefetcher->fill(victim_desc);
                break;

            case cc::is_l2c:
                this->_prefetcher->fill(victim_desc);
                break;

            case cc::is_sdc:
                this->_prefetcher->fill(victim_desc);
                break;

            case cc::is_llc:
                // this->_llc_prefetcher_operate ();
                break;
        }

        // Update replacement policy.
        desc.cpu = fill_cpu;
        desc.set = set;
        desc.way = way;
        desc.full_addr = curr_packet.full_addr;
        desc.full_vaddr = curr_packet.full_v_addr;
        desc.victim_addr = victim_block.full_addr;
        desc.pc = curr_packet.ip;
        desc.type = curr_packet.fill_level;
        desc.hit = false;
        desc.is_data = (curr_packet.is_data == 1);
        desc.lq_index = curr_packet.lq_index;

        this->_update_replacement_state(desc);

        // WIP: using the fill path stack to direct the block.
        if (!curr_packet.fill_path.empty()) {
            // If the fill path is not empty, we carry on.
            curr_packet.pop_fill_path()->return_data(curr_packet);
        } else if (curr_packet.fill_path.empty() &&
                   curr_packet.fill_level == cc::cache::fill_metadata) {
            // The fill is empty, we are on a LLC fill and we eventually need to
            // fill the Catalog Cache.
            curr_cpu->_mm.return_data(curr_packet);
        }

        // If that is a fill in the L1D, we can check the correctness of the
        // prediction.
        if (this->check_type(cc::is_l1d)) {
            // curr_cpu->_mm.check_correctness(curr_packet);
        }

        // Update processed packets.
        switch (this->_cache_type) {
            case cc::is_itlb:
                curr_packet.instruction_pa = this->_blocks[set][way].data;
                break;

            case cc::is_dtlb:
                curr_packet.data_pa = this->_blocks[set][way].data;
                break;

            case cc::is_l1i:
            case cc::is_l1d:
            case cc::is_sdc:
                if ((curr_packet.type != cc::cache::prefetch &&
                     curr_packet.fill_level != cc::cache::fill_dclr) &&
                    !this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
                break;
        }

        // In any case, we have to add the treated MSHR entry to the processed
        // queue. if (!this->_processed->is_full ()) {
        // 	this->_processed->add_queue (&curr_packet);
        // }

        // Collecting stats.
        if (champsim::simulator::instance()
                ->modeled_cpu(curr_packet.cpu)
                ->is_irreg_data(curr_packet.full_v_addr)) {
            this->_stats[fill_cpu]
                        [static_cast<cc::cache::access_types>(curr_packet.type)]
                            .irreg_data_miss++;
        } else {
            this->_stats[fill_cpu]
                        [static_cast<cc::cache::access_types>(curr_packet.type)]
                            .reg_data_miss++;
        }

        this->_stats[fill_cpu]
                    [static_cast<cc::cache::access_types>(curr_packet.type)]
                        .miss++;
        this->_stats[fill_cpu]
                    [static_cast<cc::cache::access_types>(curr_packet.type)]
                        .access++;

        // Retiring the MSHR entry.
        *it = PACKET();

        this->_writes_avail_cycle--;
    }
}

/**
 * @brief Handles as many read request as possible. The read requests are read
 * from the read queue of the cache. On a hit, the cache replacement and
 * prefetcher states are updated. On a miss, the request is added to the MSHR
 * and passed to the lower level in the memory hierarchy.
 */
void cc::sectored_cache::_handle_read() {
    uint16_t read_cpu, way;
    uint32_t set, block;
    bool continue_read = true;
    helpers::cache_access_descriptor desc;
    O3_CPU* curr_cpu = nullptr;

    while (continue_read && this->_reads_avail_cycle != 0) {
        PACKET& curr_packet = (*this->_read_queue)[this->_read_queue->head];

        read_cpu = curr_packet.cpu;

        curr_cpu = champsim::simulator::instance()->modeled_cpu(read_cpu);

        // Handling the oldest entry in the read queue.
        if (this->_read_queue->occupancy == 0 ||
            curr_packet.event_cycle > curr_cpu->current_core_cycle()) {
            return;
        }

        set = this->get_set(curr_packet.full_addr);
        block = this->get_block(curr_packet.full_addr);
        way = this->get_way(curr_packet.full_addr, set);

        // Read hit.
        if (way != this->_associativity_degree) {
            // Setting the place from where so request has been served.
            curr_packet.served_from = curr_packet.served_from =
                static_cast<cc::cache_type>(this->_cache_type);

            if (this->_cache_type == cc::is_itlb) {
                curr_packet.instruction_pa = this->_blocks[set][way].data;

                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            } else if (this->_cache_type == cc::is_dtlb) {
                curr_packet.data_pa = this->_blocks[set][way].data;

                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            } else if (this->_cache_type == cc::is_stlb) {
                curr_packet.data = this->_blocks[set][way].data;
            } else if (this->_cache_type == cc::is_l1i) {
                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            } else if (this->_cache_type == cc::is_l1d &&
                       curr_packet.type != cc::cache::prefetch) {
                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            } else if (this->_cache_type == cc::is_sdc &&
                       curr_packet.type != cc::cache::prefetch) {
                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            }

            // Update prefetcher on load instruction.
            if (curr_packet.type == cc::cache::load) {
                cp::prefetch_request_descriptor p_desc;

                switch (this->_cache_type) {
                    case cc::is_l1i:
                        this->_l1i_prefetcher_cache_operate();
                        break;

                    case cc::is_l1d:
                        p_desc.hit = true;
                        p_desc.went_offchip_pred =
                            curr_packet.went_offchip_pred;
                        p_desc.access_type =
                            static_cast<cc::cache::access_types>(
                                curr_packet.type);
                        p_desc.cpu = curr_packet.cpu;
                        p_desc.size = curr_packet.memory_size;
                        p_desc.addr =
                            this->_blocks[set][this->_sectoring_degree * way]
                                .full_addr
                            << this->log2_block_size();
                        p_desc.ip = curr_packet.ip;

                        this->_prefetcher->operate(p_desc);
                        break;

                    case cc::is_sdc:
                        p_desc.hit = true;
                        p_desc.went_offchip_pred =
                            curr_packet.went_offchip_pred;
                        p_desc.access_type =
                            static_cast<cc::cache::access_types>(
                                curr_packet.type);
                        p_desc.cpu = curr_packet.cpu;
                        p_desc.size = curr_packet.memory_size;
                        p_desc.addr =
                            this->_blocks[set][this->_sectoring_degree * way]
                                .full_addr &
                            ~this->_offset_mask;
                        p_desc.ip = curr_packet.ip;

                        this->_prefetcher->operate(p_desc);
                        break;

                    case cc::is_l2c:
                        p_desc.hit = true;
                        p_desc.went_offchip_pred =
                            curr_packet.went_offchip_pred;
                        p_desc.access_type =
                            static_cast<cc::cache::access_types>(
                                curr_packet.type);
                        p_desc.cpu = curr_packet.cpu;
                        p_desc.size = curr_packet.memory_size;
                        p_desc.addr =
                            this->_blocks[set][this->_sectoring_degree * way]
                                .address
                            << this->log2_block_size();
                        p_desc.ip = curr_packet.ip;
                        // this->_l2c_prefetcher_operate (read_cpu,
                        // this->_blocks[set][this->_associativity_degree *
                        // way].address << LOG2_BLOCK_SIZE, curr_packet.ip,
                        // curr_packet.memory_size, true,
                        // static_cast<cc::cache::access_types>
                        // (curr_packet.type));
                        this->_prefetcher->operate(p_desc);
                        break;

                    case cc::is_llc:
                        // this->_llc_prefetcher_operate (read_cpu,
                        // curr_packet.full_addr, curr_packet.ip,
                        // curr_packet.memory_size, true,
                        // static_cast<cc::cache::access_types>
                        // (curr_packet.type));
                        break;
                }
            }

            // Before going away, we should update the correspong footprint
            // bitmap.
            this->_fill_footprint_bitmap(
                set, way, curr_packet.full_addr & (this->_sector_size - 1ULL),
                curr_packet.memory_size);

            // Filling the cache access descriptor.
            desc.cpu = read_cpu;
            desc.set = set;
            desc.way = way;
            desc.pc = curr_packet.ip;
            desc.full_addr = curr_packet.full_addr;
            desc.full_vaddr = curr_packet.full_v_addr;
            desc.lq_index = curr_packet.lq_index;
            desc.victim_addr = 0ULL;
            desc.type = curr_packet.type;
            desc.hit = true;
            desc.is_data = (curr_packet.is_data == 1);
            desc.lq_index = curr_packet.lq_index;

            this->_update_replacement_state(desc);

            // Collect stats.
            if (champsim::simulator::instance()
                    ->modeled_cpu(curr_packet.cpu)
                    ->is_irreg_data(curr_packet.full_v_addr)) {
                this->_stats[read_cpu][static_cast<cc::cache::access_types>(
                                           curr_packet.type)]
                    .irreg_data_hit++;
            } else {
                this->_stats[read_cpu][static_cast<cc::cache::access_types>(
                                           curr_packet.type)]
                    .reg_data_hit++;
            }

            this->_stats[read_cpu]
                        [static_cast<cc::cache::access_types>(curr_packet.type)]
                            .hit++;
            this->_stats[read_cpu]
                        [static_cast<cc::cache::access_types>(curr_packet.type)]
                            .access++;

            // WIP: using the fill path stack to direct the block.
            if (!curr_packet.fill_path.empty()) {
                // If the fill path is not empty, we carry on.
                curr_packet.pop_fill_path()->return_data(curr_packet);
            }

            // Update prefetch stats and reset prefetch bit.
            if (this->_blocks[set][way].prefetch) {
                this->_pf_useful++;
                // WIP: Here we take notes of the location from which the
                // prefetch was served (L2C, LLC, DRAM?).
                this->_pf_useful_per_loc[this->_blocks[set][way].served_from]++;

                this->_blocks[set][way].prefetch = 0;
            }
            this->_blocks[set][way].used = 1;

            /*
             * Stats on off-chip prediction.
             *
             * Here, we gather stat on the the number of L1D hit that avoided a
             * Hermes request.
             */
            if (this->check_type(cc::is_l1d)) {
                bool went_offchip_pred =
                    curr_cpu->LQ.entry[curr_packet.lq_index].went_offchip_pred;

                ITERATE_SET(merged, curr_packet.lq_index_depend_on_me,
                            LQ_SIZE) {
                    went_offchip_pred |=
                        curr_cpu->LQ.entry[merged].went_offchip_pred;
                }

                if (went_offchip_pred)
                    curr_cpu->_mm.pbp().l1d_hits_on_offchip_pred++;
            } else if (this->check_type(cc::is_l2c)) {
                bool went_offchip_pred =
                    (curr_cpu->LQ.entry[curr_packet.lq_index]
                         .went_offchip_pred &&
                     !curr_cpu->LQ.entry[curr_packet.lq_index]
                          .l1d_miss_offchip_pred);

                ITERATE_SET(merged, curr_packet.lq_index_depend_on_me,
                            LQ_SIZE) {
                    went_offchip_pred |=
                        (curr_cpu->LQ.entry[merged].went_offchip_pred &&
                         !curr_cpu->LQ.entry[merged].l1d_miss_offchip_pred);
                }

                if (went_offchip_pred)
                    curr_cpu->_mm.pbp().l2c_hits_on_offchip_pred++;
            } else if (this->check_type(cc::is_llc)) {
                bool went_offchip_pred =
                    (curr_cpu->LQ.entry[curr_packet.lq_index]
                         .went_offchip_pred &&
                     !curr_cpu->LQ.entry[curr_packet.lq_index]
                          .l1d_miss_offchip_pred);

                ITERATE_SET(merged, curr_packet.lq_index_depend_on_me,
                            LQ_SIZE) {
                    went_offchip_pred |=
                        (curr_cpu->LQ.entry[merged].went_offchip_pred &&
                         !curr_cpu->LQ.entry[merged].l1d_miss_offchip_pred);
                }

                if (went_offchip_pred)
                    curr_cpu->_mm.pbp().llc_hits_on_offchip_pred++;
            }

            // Remove this entry from the read queue.
            this->_read_queue->remove_queue(&curr_packet);

            this->_reads_avail_cycle--;
        } else {  // Read miss.
            // Checking the MSHR.
            mshr_iterator mshr_entry = this->find_mshr(curr_packet);
            bool mshr_full = this->mshr_full();

            // This is a new miss and there is space available in the MSHR.
            if (mshr_entry == this->_mshr.end() && !mshr_full) {
                cc::locmap_prediction_descriptor pred_desc;

                // On a miss in the L1D, we get a cache level prediction.
                if (this->check_type(cc::is_l1d)) {
                    bool onchip_pred = !curr_cpu->LQ.entry[curr_packet.lq_index]
                                            .went_offchip_pred;
                    // pred_desc = champsim::simulator::instance()
                    //                 ->modeled_cpu(read_cpu)
                    //                 ->_mm.predict_desc(curr_packet.ip,
                    //                                    curr_packet.full_addr);
                    // pred_desc = curr_cpu->_mm.predict_desc(
                    //     curr_packet, curr_packet.ip, curr_packet.full_addr,
                    //     curr_packet.lq_index);

                    // If POPET predicted the block to be onchip we keep it safe
                    // and force the request to be forwarded to the L2C.
                    // Otherwise, we try to speed-up the process of fetching the
                    // data from the lower levels of the cache hierarchy by
                    // placing the request in the location predicted by the LP
                    // design.
                    // if (onchip_pred) {
                    //     pred_desc.location = cc::is_in_l2c;
                    //     pred_desc.metadata_cache_hit = true;
                    // }

                    // pred_desc.location = cc::is_in_l2c;
                    pred_desc.metadata_cache_hit = true;

                    // WIP: The idea here is to decide whether to launch an
                    // Hermes request upon an L1D miss.
                    pred_desc.location =
                        (curr_cpu->offchip_pred->consume_from_l1d(
                             curr_packet.lq_index) &&
                                 !curr_cpu->offchip_pred->consume_from_core(
                                     curr_packet.lq_index) &&
                                 curr_packet.went_offchip_pred
                             ? cc::is_in_dram
                             : cc::is_in_l2c);

                    if (pred_desc.metadata_cache_hit) {
                        curr_packet.route =
                            cpol::abstract_fill_path_policy::route(
                                pred_desc.location);

                        continue_read =
                            curr_cpu->fill_path_policy->propagate_miss(
                                this, curr_packet);
                    } else {
                        for (cc::block_location& loc : pred_desc.destinations) {
                            PACKET cpy = curr_packet;
                            // cpy.fill_level = cc::prev_fill_level(loc);
                            cpy.route =
                                cpol::abstract_fill_path_policy::route(loc);

                            continue_read =
                                curr_cpu->fill_path_policy->propagate_miss(this,
                                                                           cpy);
                        }
                    }

                    // WIP: Updating the prefetching PSEL based on the
                    // went_offchip_pred bit.
                    if (curr_cpu->LQ.entry[curr_packet.lq_index]
                            .went_offchip_pred) {
                        this->increment_pref_pred(curr_packet.full_addr >>
                                                  LOG2_PAGE_SIZE);
                    } else {
                        this->decrement_pref_pred(curr_packet.full_addr >>
                                                  LOG2_PAGE_SIZE);
                    }
                } else {
                    continue_read = curr_cpu->fill_path_policy->propagate_miss(
                        this, curr_packet);
                }

                // TODO: Stats on bypass predictor.
            } else if (mshr_entry == this->_mshr.end() &&
                       mshr_full) {  // New miss but the MSHR is full.
                continue_read = false;
            } else {  // This miss already exist. Thus we merge them.
                this->merge_mshr_on_read(curr_packet, mshr_entry);
            }

            if (continue_read) {
                // Update prefetcher on load instructions.
                if (curr_packet.type == cc::cache::load) {
                    cp::prefetch_request_descriptor p_desc;

                    switch (this->_cache_type) {
                        case cc::is_l1i:
                            this->_l1i_prefetcher_cache_operate();
                            break;
                        case cc::is_l1d:
                            p_desc.hit = false;
                            p_desc.went_offchip_pred =
                                curr_packet.went_offchip_pred;
                            p_desc.access_type =
                                static_cast<cc::cache::access_types>(
                                    curr_packet.type);
                            p_desc.cpu = curr_packet.cpu;
                            p_desc.size = curr_packet.memory_size;
                            p_desc.addr = curr_packet.address
                                          << this->log2_block_size();
                            p_desc.ip = curr_packet.ip;

                            this->_prefetcher->operate(p_desc);
                            break;
                        case cc::is_sdc:
                            p_desc.hit = false;
                            p_desc.went_offchip_pred =
                                curr_packet.went_offchip_pred;
                            p_desc.access_type =
                                static_cast<cc::cache::access_types>(
                                    curr_packet.type);
                            p_desc.cpu = curr_packet.cpu;
                            p_desc.size = curr_packet.memory_size;
                            p_desc.addr =
                                curr_packet.full_addr & ~this->_offset_mask;
                            p_desc.ip = curr_packet.ip;

                            this->_prefetcher->operate(p_desc);
                            break;
                        case cc::is_l2c:
                            p_desc.hit = false;
                            p_desc.went_offchip_pred =
                                curr_packet.went_offchip_pred;
                            p_desc.access_type =
                                static_cast<cc::cache::access_types>(
                                    curr_packet.type);
                            p_desc.cpu = curr_packet.cpu;
                            p_desc.size = curr_packet.memory_size;
                            p_desc.addr = curr_packet.address
                                          << this->log2_block_size();
                            p_desc.ip = curr_packet.ip;
                            // this->_l2c_prefetcher_operate (read_cpu,
                            // curr_packet.address << LOG2_BLOCK_SIZE,
                            // curr_packet.ip, curr_packet.memory_size, false,
                            // static_cast<cc::cache::access_types>
                            // (curr_packet.type));
                            this->_prefetcher->operate(p_desc);
                            break;
                        case cc::is_llc:
                            // this->_llc_prefetcher_operate (read_cpu,
                            // curr_packet.full_addr, curr_packet.ip,
                            // curr_packet.memory_size, false,
                            // static_cast<cc::cache::access_types>
                            // (curr_packet.type));
                            break;
                    }
                }

                // Removing this entry from the read queue.
                this->_read_queue->remove_queue(&curr_packet);

                this->_reads_avail_cycle--;
            }
        }
    }
}

void cc::sectored_cache::_handle_writeback() {
    uint16_t writeback_cpu, way;
    uint32_t set, block;
    bool continue_write = true, evict_dirty = false;
    BLOCK* victim_block;
    helpers::cache_access_descriptor desc, victim_desc;
    O3_CPU* curr_cpu = nullptr;

    while (continue_write && this->_writes_avail_cycle != 0) {
        PACKET& curr_packet = (*this->_write_queue)[this->_write_queue->head];
        writeback_cpu = curr_packet.cpu;
        curr_cpu = champsim::simulator::instance()->modeled_cpu(writeback_cpu);

        // Handle the oldest entry in the write queue.
        if (this->_write_queue->occupancy == 0 || writeback_cpu >= NUM_CPUS ||
            curr_packet.event_cycle > curr_cpu->current_core_cycle()) {
            return;
        }

        set = this->get_set(curr_packet.full_addr);
        block = this->get_block(curr_packet.full_addr);

        // Writeback hit.
        if (this->_is_hit(curr_packet, set)) {
            way = this->get_way(curr_packet.full_addr, set);

            // Setting the place from where so request has been served.
            curr_packet.served_from = curr_packet.served_from =
                static_cast<cc::cache_type>(this->_cache_type);

            if (this->_cache_type == cc::is_itlb) {
                curr_packet.instruction_pa = this->_blocks[set][way].data;

                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            } else if (this->_cache_type == cc::is_dtlb) {
                curr_packet.data_pa = this->_blocks[set][way].data;

                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            } else if (this->_cache_type == cc::is_stlb) {
                curr_packet.data = this->_blocks[set][way].data;
            } else if (this->_cache_type == cc::is_l1i) {
                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            } else if (this->_cache_type == cc::is_l1d &&
                       curr_packet.type != cc::cache::prefetch) {
                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            } else if (this->_cache_type == cc::is_sdc &&
                       curr_packet.type != cc::cache::prefetch) {
                if (!this->_processed->is_full()) {
                    this->_processed->add_queue(&curr_packet);
                }
            }

            // Before going away, we should update the correspong footprint
            // bitmap.
            this->_fill_footprint_bitmap(
                set, way, curr_packet.full_addr & (this->_sector_size - 1ULL),
                curr_packet.memory_size);

            // Filling the access descriptor.
            desc.cpu = writeback_cpu;
            desc.set = set;
            desc.way = way;
            desc.pc = curr_packet.ip;
            desc.full_addr = curr_packet.full_addr;
            desc.full_vaddr = curr_packet.full_v_addr;
            desc.victim_addr = 0ULL;
            desc.type = curr_packet.type;
            desc.hit = true;
            desc.is_data = (curr_packet.is_data == 1);
            desc.lq_index = curr_packet.lq_index;

            this->_update_replacement_state(desc);

            // Collect stats.
            if (champsim::simulator::instance()
                    ->modeled_cpu(curr_packet.cpu)
                    ->is_irreg_data(curr_packet.full_v_addr)) {
                this->_stats[writeback_cpu]
                            [static_cast<cc::cache::access_types>(
                                 curr_packet.type)]
                                .irreg_data_hit++;
            } else {
                this->_stats[writeback_cpu]
                            [static_cast<cc::cache::access_types>(
                                 curr_packet.type)]
                                .reg_data_hit++;
            }

            this->_stats[writeback_cpu]
                        [static_cast<cc::cache::access_types>(curr_packet.type)]
                            .hit++;
            this->_stats[writeback_cpu]
                        [static_cast<cc::cache::access_types>(curr_packet.type)]
                            .access++;

            // Mark this block as dirty.
            this->_fill_dirty_bits(set, way,
                                   curr_packet.full_addr & this->_offset_mask,
                                   curr_packet.memory_size, true);

            switch (this->_cache_type) {
                case cc::is_itlb:
                    curr_packet.instruction_pa = this->_blocks[set][way].data;
                    break;

                case cc::is_dtlb:
                    curr_packet.data_pa = this->_blocks[set][way].data;
                    break;

                case cc::is_stlb:
                    curr_packet.data = this->_blocks[set][way].data;
                    break;
            }

            // WIP: using the fill path stack to direct the block.
            if (!curr_packet.fill_path.empty()) {
                // If the fill path is not empty, we carry on.
                curr_packet.pop_fill_path()->return_data(curr_packet);
            } else if (curr_packet.fill_path.empty() &&
                       curr_packet.fill_level == cc::cache::fill_metadata) {
                // The fill is empty, we are on a LLC fill and we eventually
                // need to fill the Catalog Cache.
                curr_cpu->_mm.return_data(curr_packet);
            }

            this->_writes_avail_cycle--;

            // Remove this entry from the write queue.
            this->_write_queue->remove_queue(&curr_packet);
        } else {  // Writeback miss
            if (this->_cache_type == cc::is_l1d) {
                // if (this->_cache_type == cc::is_l1d &&
                // curr_cpu->write_alloc_psel > 0) { if (false) { if (true) { if
                // (!this->check_type(cc::is_llc)) {
                mshr_iterator mshr_entry = this->find_mshr(curr_packet);
                bool mshr_full = this->mshr_full();

                cc::locmap_prediction_descriptor pred_desc;

                // pred_desc = champsim::simulator::instance()
                //                 ->modeled_cpu(writeback_cpu)
                //                 ->_mm.predict_desc(curr_packet.full_addr);

                // Forcing the prediction to use L2C path.
                pred_desc.location = cc::is_in_l2c;
                pred_desc.metadata_cache_hit = true;

                curr_packet.route =
                    cpol::abstract_fill_path_policy::route(pred_desc.location);

                if (mshr_entry == this->_mshr.end() && !mshr_full) {
                    continue_write = curr_cpu->fill_path_policy->propagate_miss(
                        this, curr_packet);

                    // TODO: Stats on bypass predictor.
                } else if (mshr_entry == this->_mshr.end() && mshr_full) {
                    // No space available in the MSHR. We have to drop the
                    // process until there is space available.
                    continue_write = false;
                } else {  // Already in-flight miss.
                    this->merge_mshr_on_writeback(curr_packet, mshr_entry);
                }
            } else {
                // Check MSHR.
                block = this->get_block(curr_packet.full_addr);

                // Filling the descriptor.
                desc.cpu = writeback_cpu;
                desc.set = set;
                desc.way = UINT8_MAX;
                desc.pc = curr_packet.ip;
                desc.full_addr = curr_packet.full_addr;
                desc.full_vaddr = curr_packet.full_v_addr;
                desc.victim_addr = 0ULL;
                desc.type = curr_packet.type;
                desc.hit = false;

                way = this->_find_victim(desc);

                victim_block =
                    &this->_blocks[set][this->_sectoring_degree * way];

                // If the lower level cache is a distill cache, we transmit the
                // footprint. if (this->_lower_level_memory) {
                // 	dynamic_cast<cc::cache *>
                // (this->_lower_level_memory)->update_footprint
                // (victim_block->full_addr, this->_footprints[set][way]);
                // }

                // this->_clear_footprint_bitmap (set, way);

                evict_dirty = (this->_lower_level_memory != nullptr &&
                               this->_is_sector_dirty(set, way));

                if (evict_dirty) {
                    // As in this part of the function, we could be any of the
                    // L1I, L2C and LLC. Thus, we should check it.
                    if (!(this->check_type(cc::is_llc) ||
                          this->check_type(cc::is_sdc))) {
                        if (this->_lower_level_memory->write_queue_occupancy(
                                victim_block->address) ==
                            this->_lower_level_memory->write_queue_size(
                                victim_block->address)) {
                            // The write queue of the lower level memory system
                            // is full, we cannot replace the victim now.
                            continue_write = false;
                        } else {
                            PACKET writeback_packet;

                            writeback_packet.fill_level = next_fill_level(
                                static_cast<cc::cache::fill_levels>(
                                    this->_fill_level));
                            writeback_packet.cpu = writeback_cpu;
                            writeback_packet.address =
                                this->_blocks[set][way].address;
                            writeback_packet.full_addr =
                                this->_blocks[set][way].full_addr &
                                ~this->_offset_mask;
                            writeback_packet.memory_size = this->_sector_size;
                            writeback_packet.data =
                                this->_blocks[set][way].data;
                            writeback_packet.instr_id = curr_packet.instr_id;
                            writeback_packet.ip = 0;
                            writeback_packet.type =
                                static_cast<uint32_t>(cc::cache::writeback);
                            writeback_packet.event_cycle =
                                curr_cpu->current_core_cycle();

                            // Writing back this block in the lower level cache.
                            this->_lower_level_memory->add_write_queue(
                                writeback_packet);
                        }
                    } else if (this->check_type(cc::is_sdc)) {
                        PACKET writeback_packet;

                        writeback_packet.fill_level =
                            next_fill_level(static_cast<cc::cache::fill_levels>(
                                this->_fill_level));
                        writeback_packet.cpu = writeback_cpu;
                        writeback_packet.address =
                            this->_blocks[set][way].address;
                        writeback_packet.full_addr =
                            this->_blocks[set][way].full_addr &
                            ~this->_offset_mask;
                        writeback_packet.memory_size = this->_sector_size;
                        writeback_packet.data = this->_blocks[set][way].data;
                        writeback_packet.instr_id = curr_packet.instr_id;
                        writeback_packet.ip = 0;
                        writeback_packet.type =
                            static_cast<uint32_t>(cc::cache::writeback);
                        writeback_packet.event_cycle =
                            curr_cpu->current_core_cycle();

                        if (!this->_handle_sdc_miss_propagation(
                                writeback_packet)) {
                            continue_write = false;
                        }
                    } else if (this->check_type(cc::is_llc)) {
                        if (this->_dram->get_occupancy(2,
                                                       victim_block->address) ==
                            this->_dram->get_size(2, victim_block->address)) {
                            // The write queue of the lower level memory system
                            // is full, we cannot replace the victim now.
                            continue_write = false;
                        } else {
                            PACKET writeback_packet;

                            writeback_packet.fill_level = next_fill_level(
                                static_cast<cc::cache::fill_levels>(
                                    this->_fill_level));
                            writeback_packet.cpu = writeback_cpu;
                            writeback_packet.address =
                                this->_blocks[set][way].address;
                            writeback_packet.full_addr =
                                this->_blocks[set][way].full_addr &
                                ~this->_offset_mask;
                            writeback_packet.memory_size = this->_sector_size;
                            writeback_packet.data =
                                this->_blocks[set][way].data;
                            writeback_packet.instr_id = curr_packet.instr_id;
                            writeback_packet.ip = 0;
                            writeback_packet.type =
                                static_cast<uint32_t>(cc::cache::writeback);
                            writeback_packet.event_cycle =
                                curr_cpu->current_core_cycle();

                            // Writing back this block in the lower level cache.
                            this->_dram->add_wq(&writeback_packet);
                        }
                    }
                }

                if (continue_write) {
                    victim_desc.victim_addr = victim_block->full_addr;

                    // Update prefetcher.
                    switch (this->_cache_type) {
                        case cc::is_l1i:
                            // this->_l1i_prefetcher_cache_operate ();
                            break;

                        case cc::is_l1d:
                            this->_prefetcher->fill(victim_desc);
                            break;

                        case cc::is_l2c:
                            this->_prefetcher->fill(victim_desc);
                            break;

                        case cc::is_sdc:
                            this->_prefetcher->fill(victim_desc);
                            break;

                        case cc::is_llc:
                            // this->_llc_prefetcher_operate ();
                            break;
                    }

                    // Update replacement policy.
                    desc.cpu = writeback_cpu;
                    desc.set = set;
                    desc.way = way;
                    desc.pc = curr_packet.ip;
                    desc.full_addr = curr_packet.full_addr;
                    desc.full_vaddr = curr_packet.full_v_addr;
                    desc.victim_addr = victim_block->full_addr;
                    desc.type = curr_packet.type;
                    desc.hit = false;
                    desc.is_data = (curr_packet.is_data == 1);
                    desc.lq_index = curr_packet.lq_index;

                    this->_update_replacement_state(desc);

                    //	Collect stats.
                    if (champsim::simulator::instance()
                            ->modeled_cpu(curr_packet.cpu)
                            ->is_irreg_data(curr_packet.full_v_addr)) {
                        this->_stats[writeback_cpu]
                                    [static_cast<cc::cache::access_types>(
                                         curr_packet.type)]
                                        .irreg_data_miss++;
                    } else {
                        this->_stats[writeback_cpu]
                                    [static_cast<cc::cache::access_types>(
                                         curr_packet.type)]
                                        .reg_data_miss++;
                    }

                    this->_stats[writeback_cpu]
                                [static_cast<cc::cache::access_types>(
                                     curr_packet.type)]
                                    .miss++;
                    this->_stats[writeback_cpu]
                                [static_cast<cc::cache::access_types>(
                                     curr_packet.type)]
                                    .access++;

                    this->_fill_cache(set, way, curr_packet);

                    // Mark dirty.
                    this->_fill_dirty_bits(
                        set, way, curr_packet.full_addr & this->_offset_mask,
                        curr_packet.memory_size, true);

                    // WIP: using the fill path stack to direct the block.
                    if (!curr_packet.fill_path.empty()) {
                        // If the fill path is not empty, we carry on.
                        curr_packet.pop_fill_path()->return_data(curr_packet);
                    }
                }
            }

            if (continue_write) {
                this->_writes_avail_cycle--;

                // Remove this entry from the write queue.
                this->_write_queue->remove_queue(&curr_packet);
            }
        }
    }
}

void cc::sectored_cache::_handle_prefetch() {
    bool continue_prefetch = true;
    uint16_t prefetch_cpu, way;
    uint32_t set, block;
    helpers::cache_access_descriptor desc;
    O3_CPU* curr_cpu = nullptr;

    while (continue_prefetch && this->_reads_avail_cycle != 0) {
        PACKET& curr_packet =
            (*this->_prefetch_queue)[this->_prefetch_queue->head];
        prefetch_cpu = curr_packet.cpu;
        curr_cpu = champsim::simulator::instance()->modeled_cpu(prefetch_cpu);

        // Handle the oldest entry in the prefetch queue.
        if (this->_prefetch_queue->occupancy == 0 || prefetch_cpu >= NUM_CPUS ||
            curr_packet.event_cycle > curr_cpu->current_core_cycle()) {
            return;
        }

        set = this->get_set(curr_packet.full_addr);
        block = this->get_block(curr_packet.full_addr);
        way = this->get_way(curr_packet.full_addr, set);

        // Prefetch hit.
        if (this->_is_hit(curr_packet, set)) {
            // Setting the place from where so request has been served.
            curr_packet.served_from = curr_packet.served_from =
                static_cast<cc::cache_type>(this->_cache_type);

            // Filling the cache access descriptor.
            desc.cpu = prefetch_cpu;
            desc.set = set;
            desc.way = way;
            desc.pc = curr_packet.ip;
            desc.full_addr = curr_packet.full_addr;
            desc.full_vaddr = curr_packet.full_v_addr;
            desc.victim_addr = 0ULL;
            desc.type = curr_packet.type;
            desc.hit = true;
            desc.is_data = (curr_packet.is_data == 1);
            desc.lq_index = curr_packet.lq_index;

            this->_update_replacement_state(desc);

            // Collect stats.
            if (champsim::simulator::instance()
                    ->modeled_cpu(curr_packet.cpu)
                    ->is_irreg_data(curr_packet.full_v_addr)) {
                this->_stats[prefetch_cpu][static_cast<cc::cache::access_types>(
                                               curr_packet.type)]
                    .irreg_data_hit++;
            } else {
                this->_stats[prefetch_cpu][static_cast<cc::cache::access_types>(
                                               curr_packet.type)]
                    .reg_data_hit++;
            }

            this->_stats[prefetch_cpu]
                        [static_cast<cc::cache::access_types>(curr_packet.type)]
                            .hit++;
            this->_stats[prefetch_cpu]
                        [static_cast<cc::cache::access_types>(curr_packet.type)]
                            .access++;

            if (curr_packet.pf_origin_level < this->_fill_level) {
                curr_cpu->fill_path_policy->prefetch_on_higher_prefetch_on_hit(
                    this, curr_packet);
            }

            // WIP: using the fill path stack to direct the block.
            if (!curr_packet.fill_path.empty()) {
                // If the fill path is not empty, we carry on.
                curr_packet.pop_fill_path()->return_data(curr_packet);
            }

            // WIP: Training the prefetch-specific offchip predictor.
            if (curr_packet.pf_origin_level == cc::cache::fill_l1) {
                assert(!curr_packet.pf_went_offchip);

                curr_cpu->offchip_pred->train_on_prefetch(curr_packet);
            }

            // Removing this entry from the read queue.
            this->_prefetch_queue->remove_queue(&curr_packet);

            this->_reads_avail_cycle--;
        } else {  // Prefetch miss.
            mshr_iterator mshr_entry = this->find_mshr(curr_packet);
            bool mshr_full = this->mshr_full();

            // This is a new miss and there is space available in the MSHR.
            if (mshr_entry == this->_mshr.end() && !mshr_full) {
                cc::locmap_prediction_descriptor pred_desc = {
                    .metadata_cache_hit = true,
                    .location = cc::is_in_l2c,
                };

                if (this->check_type(cc::is_l1d)) {
                    // pred_desc = curr_cpu->_mm.predict_desc_on_prefetch(
                    //     curr_packet, curr_packet.ip, curr_packet.full_addr,
                    //     0);
                    // pred_desc.metadata_cache_hit = true;
                    // pred_desc.location = cc::is_in_l2c;

                    // if (pred_desc.metadata_cache_hit) {
#if defined(ENABLE_SSP)
                    if (!curr_packet.pf_went_offchip_pred) {
#endif  // defined(ENABLE_SSP)
                        curr_packet.route =
                            cpol::abstract_fill_path_policy::route(
                                cc::is_in_l2c);

                        continue_prefetch =
                            curr_cpu->fill_path_policy->propagate_miss(
                                this, curr_packet);
#if defined(ENABLE_SSP)
                    }
#endif  // defined(ENABLE_SSP)
                } else {
                    // SANITY: Here, we are either in the L2C or the LLC and a
                    // prefetch request missed in this cache. If this requested
                    // originated from the L1D, the pf_went_offchip_pred bit
                    // must be set to 0 as this indicates that the prefetched
                    // block is expected to be found onchip.
#ifndef NDEBUG
                    // if (curr_packet.pf_origin_level == cc::cache::fill_l1)
                    //     assert(!curr_packet.pf_went_offchip_pred);
#endif  // NDEBUG
                    // WIP: Upon a prefetch LLC miss, we block prefetch requests from going
                    // down to the DRAM.
                    if (!this->check_type(cc::is_llc)) {
                        continue_prefetch =
                            curr_cpu->fill_path_policy->propagate_miss(this,
                                                                    curr_packet);
                    } else { // If this is the LLC, we need to back to the previous levels and eliminate that prefetch request.
                        const PACKET &curr_packet_cpy = curr_packet;

                        curr_cpu->l2c->prefetch_queue()->remove_queue(&curr_packet);
                        curr_cpu->l1d->prefetch_queue()->remove_queue(&curr_packet);

                        std::remove_if(curr_cpu->l2c->mshr().begin(), curr_cpu->l2c->mshr().end(), [curr_packet] (const PACKET& e) {
                            return e.address == curr_packet.address && e.type == cc::cache::prefetch;
                        });
                        std::remove_if(curr_cpu->l1d->mshr().begin(), curr_cpu->l1d->mshr().end(), [curr_packet] (const PACKET& e) {
                            return e.address == curr_packet.address && e.type == cc::cache::prefetch;
                        });
                    }
                }

                // Generate subsequent prefetches on prefetch misses originating
                // from a higher level.
                if (continue_prefetch &&
                    curr_packet.fill_level < this->_fill_level) {
                    curr_cpu->fill_path_policy
                        ->prefetch_on_higher_prefetch_on_miss(this,
                                                              curr_packet);
                }
            } else if (mshr_entry == this->_mshr.end() &&
                       mshr_full) {  // New miss but the MSHR is full.
                continue_prefetch = false;
            } else {  // This miss already exist. Thus we merge them.
                this->merge_mshr_on_prefetch(curr_packet, mshr_entry);
            }

            if (continue_prefetch) {
                // Removing this entry from the read queue.
                this->_prefetch_queue->remove_queue(&curr_packet);

                this->_reads_avail_cycle--;
            }
        }
    }
}

bool cc::sectored_cache::_handle_l1d_miss_propagation(
    PACKET& packet, const cc::block_location& target) {
    bool ret = false;
    cc::sdc_routes route = cc::invalid_route;

    // What route must be used?
    switch (target) {
        case cc::is_in_l2c:
        case cc::is_in_both:
            route = cc::sdc_l2c_dram;
            break;
        case cc::is_in_llc:
            route = cc::l1d_llc;
            break;
        case cc::is_in_dram:
            route = cc::l1d_dram;
            break;

        // TODO: This is simply undefined behaviour. Should we throw an
        // exception?
        default:
            break;
    }

    packet.bypassed_l2c_llc = true;

    // Now, we do the actual job.
    ret = this->_handle_miss_propagation(packet, route);

    return ret;
}

bool cc::sectored_cache::_handle_sdc_miss_propagation(PACKET& packet) {
    bool ret = false;
cc:
    sdc_routes route = cc::sdc_dram;

    // We should never call this version of this method on a writeback as they
    // are completely deterministic. if (packet.type == cc::cache::writeback) {
    // 	route = cc::sdc_dram;
    // 	// throw std::runtime_error ("The speculative version of the
    // \"_handle_sdc_miss_propagation\" should not be used on writebacks."); }
    // else { 	if (this->_re.should_sniff ()) {
    // this->_re.mark_sniffer
    // (packet); 		route = packet.route; 	} else {
    // route = this->_re.predict
    // ();
    // 	}
    // }

    // if (this->_re.should_sniff ()) {
    // 	this->_re.mark_sniffer (packet);
    // 	route = packet.route;
    // } else {
    // 	route = this->_re.predict ();
    // }

    // Now, we do the actual job.
    ret = this->_handle_miss_propagation(packet, route);

    // If the request has successfully been transmitted to a lower storage, we
    // can run some checks.
    if (ret) {
        if (!packet.sniffer) {
            this->_re.check_prediction(packet.full_addr, packet.cpu, route);
        }

        // Updating the state of the routing engine.
        this->_re.inc_packet_counter();
    }

    return ret;
}

bool cc::sectored_cache::_handle_miss_propagation(PACKET& packet,
                                                  const cc::sdc_routes& route) {
    int queue_type, occupancy, size;
    bool is_read = (packet.type == cc::cache::load ||
                    packet.type == cc::cache::prefetch),
         is_prefetch = (packet.type == cc::cache::prefetch);
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);

    // First, we assign the route.
    packet.route = route;

    // Let's propagate te packet according to the fill path policy.
    return curr_cpu->fill_path_policy->propagate_miss(this, packet);

    // We try to insert in one of the following memory components: DRAM, L2C.
    if (packet.route == cc::sdc_dram || packet.route == cc::l1d_dram) {
        /*
         * Here, we pass the packet on which the SDC missed to the DRAM
         *directly. There's a breakdown of the proceedings in this case:
         *	1. We check the occupancy of the matching queues.
         *	2. If there is still space available, we insert it in the
         *matching queues of the DRAM.
         *	3. Then we add the miss in the MSHR.
         */
        queue_type = (is_read) ? 1 : 2;

        // If the DRAM RQ/WQ is full, we stop here returning false.
        if (this->_dram->get_occupancy(queue_type, packet.address) ==
            this->_dram->get_size(queue_type, packet.address)) {
            return false;
        }

        // The DRAM queue is not full so we allocate a MSHR entry and we add the
        // packet to the lower queue.
        if (packet.type != cc::cache::writeback) {
            this->allocate_mshr(packet);
        }

        if (is_read) {
            this->_dram->add_rq(&packet);
        } else {
            this->_dram->add_wq(&packet);
        }
    } else if (packet.route == cc::sdc_l2c_dram) {
        /*
         * Here, we pass the packet on which the SDC missed to the L2C.
         * There's a breakdown of the proceedings in this case:
         *	1. We check the occupancy of the matching queue in the lower
         *level cache.
         *	2. If there is still space available, we insert it in the
         *matching queue of the L2C.
         *	3. Then we add the miss in the MSHR.
         */
        if (is_read && !is_prefetch) {
            occupancy = champsim::simulator::instance()
                            ->modeled_cpu(packet.cpu)
                            ->l2c->read_queue_occupancy();
            size = champsim::simulator::instance()
                       ->modeled_cpu(packet.cpu)
                       ->l2c->read_queue_size();
        } else if (is_read && is_prefetch) {
            occupancy = champsim::simulator::instance()
                            ->modeled_cpu(packet.cpu)
                            ->l2c->prefetch_queue_occupancy();
            size = champsim::simulator::instance()
                       ->modeled_cpu(packet.cpu)
                       ->l2c->prefetch_queue_size();
        } else if (!is_read) {
            occupancy = champsim::simulator::instance()
                            ->modeled_cpu(packet.cpu)
                            ->l2c->write_queue_occupancy();
            size = champsim::simulator::instance()
                       ->modeled_cpu(packet.cpu)
                       ->l2c->write_queue_size();
        }

        // The lower queue is full.
        if (occupancy == size) {
            return false;
        }

        // Here, the queue is not full so we can allocate an entry in the MSHR
        // and insert in the lower queue.
        if (packet.fill_level <= this->_fill_level &&
            packet.type != cc::cache::writeback) {
            this->allocate_mshr(packet);
        }

        if (is_read && !is_prefetch) {
            champsim::simulator::instance()
                ->modeled_cpu(packet.cpu)
                ->l2c->add_read_queue(packet);
        } else if (is_read && is_prefetch) {
            champsim::simulator::instance()
                ->modeled_cpu(packet.cpu)
                ->l2c->add_prefetch_queue(packet);
        } else if (!is_read) {
            champsim::simulator::instance()
                ->modeled_cpu(packet.cpu)
                ->l2c->add_write_queue(packet);
        }
    } else if (packet.route == cc::sdc_llc_dram ||
               packet.route == cc::l1d_llc) {
        /*
         * Here, we pass the packet on which the SDC missed to the LLC.
         * There's a breakdown of the proceedings in this case:
         *	1. We check the occupancy of the matching queue in the lower
         *level cache.
         *	2. If there is still space available, we insert it in the
         *matching queue of the L2C.
         *	3. Then we add the miss in the MSHR.
         */
        if (is_read && !is_prefetch) {
            occupancy = uncore.llc->read_queue_occupancy();
            size = uncore.llc->read_queue_size();
        } else if (is_read && is_prefetch) {
            occupancy = uncore.llc->prefetch_queue_occupancy();
            size = uncore.llc->prefetch_queue_size();
        } else if (!is_read) {
            occupancy = uncore.llc->write_queue_occupancy();
            size = uncore.llc->write_queue_size();
        }

        // The lower queue is full.
        if (occupancy == size) {
            return false;
        }

        // Here, the queue is not full so we can allocate an entry in the MSHR
        // and insert in the lower queue.
        if (packet.fill_level <= this->_fill_level &&
            packet.type != cc::cache::writeback) {
            this->allocate_mshr(packet);
        }

        if (is_read && !is_prefetch) {
            uncore.llc->add_read_queue(packet);
        } else if (is_read && is_prefetch) {
            uncore.llc->add_prefetch_queue(packet);
        } else if (!is_read) {
            uncore.llc->add_write_queue(packet);
        }
    }

    return true;
}

/**
 * @brief Handles the propagation of L1D misses using a conservative approach as
 * Described in "Reducing Load Latency with Cache Level Prediction".
 *
 * @param packet The packet to propagate to one of the lower levels of the
 * memory hierarchy.
 * @param route The route to use for propagation.
 * @return true The propagation succeded and we can go ahead in the execution.
 * @return false The propagation failed and the propagation must tried again
 * later on.
 */
bool cc::sectored_cache::_handle_miss_conservative_path(
    PACKET& packet, const cc::sdc_routes& route) {}

uint32_t cc::sectored_cache::block_size() const { return this->_sector_size; }

void cc::sectored_cache::_return_data_helper(PACKET& packet) {
    bool misprediction = true;
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);
    mshr_iterator mshr_entry = this->find_mshr(packet);

    // Sanity check.
    if (packet.is_metadata && !this->check_type(cc::is_llc)) {
        throw std::runtime_error(
            "Caching LocMap entries is not allowed anywhere else than in the "
            "LLC.");
    }

    if (packet.fill_level == cc::cache::fill_dclr ||
        mshr_entry == this->_mshr.end() || mshr_entry->returned == COMPLETED) {
#ifndef NDEBUG  // WIP: Temporarily ignoring this exception.
                // throw std::logic_error("MSHR entry could\'t be found.");
#endif          // NDEBUG
        return;
    }

    // Updating the route based on the returned packet.
    assert(mshr_entry->fill_path.size() > packet.fill_path.size());
    assert(packet.fill_path.empty() || packet.fill_path.top() != this);

    /*
     * We need to unpack the top element of the fill path. Otherwise, by merging
     * it would end up on top the fill path once again leading to a loop or the
     * packet being lost.
     */
    if (mshr_entry->fill_path.top() == this) {
        mshr_entry->fill_path.pop();
    } else if (*mshr_entry->fill_path.top() > *this) {
        if (!packet.fill_path.empty()) {
            mshr_entry->pop_fill_path_until(
                [tc = packet.fill_path.top()](const PACKET::fill_path_t& fp)
                    -> bool { return (*fp.top() < *tc); });
        } else {
            mshr_entry->fill_path = PACKET::fill_path_t();
        }
    } else {
        // This doesn't make much sense. We throw an exception.
        throw std::runtime_error("Inconsistent fill path.");
    }

    mshr_entry->merge_fill_path(packet);

    if (!mshr_entry->fill_path.empty() && mshr_entry->fill_path.top() == this) {
        mshr_entry->fill_path.pop();
    }

    /*
     * Sanity check: Checking the cpu index of the top element of the fill path.
     *
     * The cpu index of the packet has to match the cpu index of the cache at
     * the top of the fill path.
     */
    if (!mshr_entry->fill_path.empty() &&
        mshr_entry->cpu != mshr_entry->fill_path.top()->cpu()) {
        throw std::runtime_error("Invalid fill path.");
    }

    // MSHR holds the most updated information about this request.
    mshr_entry->returned = COMPLETED;
    mshr_entry->data = packet.data;
    mshr_entry->pf_metadata = packet.pf_metadata;

    // Getting data related to the distill cache. They will be used if it makes
    // sense.
    mshr_entry->served_from = packet.served_from;
    mshr_entry->missed_in = packet.missed_in;

    /*
     * TODO: Because of the parallel look-up scheme implemented in ChampSim, it
     * possible for packets to lose references to instructions on the way up to
     * the cache. Thus, we should ensure that references are propagated on a
     * return.
     */
    if (packet.load_merged) {
        mshr_entry->load_merged = true;
        mshr_entry->lq_index_depend_on_me.insert(packet.lq_index);
        mshr_entry->lq_index_depend_on_me.join(packet.lq_index_depend_on_me,
                                               LQ_SIZE);
    }
    if (packet.store_merged) {
        mshr_entry->store_merged = true;
        mshr_entry->sq_index_depend_on_me.insert(packet.sq_index);
        mshr_entry->sq_index_depend_on_me.join(packet.sq_index_depend_on_me,
                                               SQ_SIZE);
    }

    if (!packet.is_metadata) {
        if (mshr_entry->event_cycle < curr_cpu->current_core_cycle()) {
            mshr_entry->event_cycle =
                curr_cpu->current_core_cycle() + this->_latency;
        } else {
            mshr_entry->event_cycle += this->_latency;
        }
    }

#if !defined(ENABLE_DCLR)
    /**
     * HERMES: Here, if the block came back from the DRAM, this means that
     * the request went offchip and we must signal it to the core's LQ entry.
     */
    if (this->check_type(cc::is_llc) && packet.type == cc::cache::load) {
        curr_cpu->LQ.entry[packet.lq_index].went_offchip = true;

        ITERATE_SET(merged, packet.lq_index_depend_on_me, LQ_SIZE) {
            curr_cpu->LQ.entry[merged].went_offchip = true;
            curr_cpu->LQ.entry[merged].l1d_offchip_pred_used |=
                curr_cpu->LQ.entry[packet.lq_index].l1d_offchip_pred_used;
            curr_cpu->LQ.entry[merged].l1d_miss_offchip_pred |=
                curr_cpu->LQ.entry[packet.lq_index].l1d_miss_offchip_pred;

            // if (packet.l1d_offchip_pred_used)
            //     std::cout << curr_cpu->LQ.entry[merged].l1d_offchip_pred_used
            //               << std::endl;
        }
    } else if (this->check_type(cc::is_llc) &&
               packet.type == cc::cache::prefetch &&
               packet.pf_origin_level == cc::cache::fill_l1) {
        mshr_entry->pf_went_offchip = true;

        curr_cpu->offchip_pred->train_on_prefetch(*mshr_entry);
    }
#endif  // !defined(ENABLE_DCLR)
}

void cc::sectored_cache::return_data(PACKET& packet) {
    if (!this->check_type(cc::is_llc) && packet.cpu != this->_cpu) {
        throw std::runtime_error("Illegal return to cache.");
    }

    this->_return_data_helper(packet);
}

void cc::sectored_cache::return_data(
    PACKET& packet, const boost::dynamic_bitset<>& valid_bits) {
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);
    auto mshr_entry =
        std::find_if(this->_mshr.begin(), this->_mshr.end(),
                     [size = _sector_size, packet](const PACKET& e) -> bool {
                         bool slow_track_a = false, slow_track_b = false,
                              same_route = false, same_cpu = false;

                         std::size_t log2_block_size =
                             static_cast<std::size_t>(std::log2(size));

                         // slow_track_a = (packet.route != cc::sdc_dram);
                         // slow_track_b = (e.route != cc::sdc_dram);
                         // same_route = !((!slow_track_a && slow_track_b) ||
                         // (slow_track_a && ! slow_track_b));
                         //
                         // if (packet.route == cc::sdc_routes_invalid ^ e.route
                         // == cc::sdc_routes_invalid) {
                         //     same_route = false;
                         // }

                         same_route = true;

                         same_cpu = (e.cpu == packet.cpu);

                         return (((e.full_addr >> log2_block_size) ==
                                  (packet.full_addr >> log2_block_size)) &&
                                 same_route && same_cpu);
                     });
    // auto mshr_entry = std::find_if (this->_mshr.begin (), this->_mshr.end (),
    // 	mshr_predicate_generator (this->_sector_size, packet));
    // auto mshr_entry = std::find_if (this->_mshr.begin (), this->_mshr.end (),
    // [&packet] (const PACKET& e) -> bool { return (e.address ==
    // packet.address); });

    // Sanity check.
    if (mshr_entry == this->_mshr.end()) {
        throw std::logic_error("MSHR entry could\'t be found.");
    }

    // MSHR holds the most updated information about this request.
    mshr_entry->returned = COMPLETED;
    mshr_entry->data = packet.data;
    mshr_entry->pf_metadata = packet.pf_metadata;

    // Getting data related to the distill cache. They will be used if it makes
    // sense.
    mshr_entry->served_from = packet.served_from;

    mshr_entry->missed_in = packet.missed_in;

    if (mshr_entry->event_cycle < curr_cpu->current_core_cycle()) {
        mshr_entry->event_cycle =
            curr_cpu->current_core_cycle() + this->_latency;
    } else {
        mshr_entry->event_cycle += this->_latency;
    }
}
