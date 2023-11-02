#include <cassert>
#
#include <utility>
#
#include <boost/dll.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <functional>
#
#include <internals/memory_class.h>
#include <internals/uncore.h>

#include <internals/components/cache.hh>
#include <internals/prefetchers/iprefetcher.hh>
#include <internals/replacements/ireplacementpolicy.hh>
#include <internals/simulator.hh>

namespace cc = champsim::components;
namespace dll = boost::dll;

static std::map<std::string, cc::cache_type> cache_type_map = {
    {"itlb", cc::is_itlb}, {"dtlb", cc::is_dtlb}, {"stlb", cc::is_stlb},
    {"l1i", cc::is_l1i},   {"l1d", cc::is_l1d},   {"l2c", cc::is_l2c},
    {"llc", cc::is_llc},   {"sdc", cc::is_sdc},
};

static std::map<std::string, cc::cache::fill_levels> fill_level_map = {
    {"fill_l1", cc::cache::fill_l1},     {"fill_l2", cc::cache::fill_l2},
    {"fill_llc", cc::cache::fill_llc},   {"fill_drc", cc::cache::fill_drc},
    {"fill_dram", cc::cache::fill_dram}, {"fill_sdc", cc::cache::fill_sdc},
};

static std::map<cc::cache::access_types, std::pair<std::string, std::string>>
    access_type_map = {
        {cc::cache::load, {"load", "LOAD"}},
        {cc::cache::rfo, {"rfo", "RFO"}},
        {cc::cache::prefetch, {"prefetch", "PREFETCH"}},
        {cc::cache::writeback, {"writeback", "WRITEBACK"}},
};

cc::cache::cache()
    : _cache_type(0x0),
      _stats(CHAMPSIM_CPU_NUMBER_CORE),
      _read_overlap(0),
      _read_received(0),
      _write_overlap(0),
      _write_received(0),
      _prefetch_overlap(0),
      _prefetch_received(0),
      _total_miss_latency(0),
      _psel_prefetching(0) {
    // Filling the stats container.
    for (std::size_t i = 0; i < CHAMPSIM_CPU_NUMBER_CORE; i++) {
        for (const auto& e : {load, rfo, writeback, prefetch}) {
            this->_stats[i].insert(std::make_pair(e, cache_stats()));
        }
    }

    // WIP: Setting up prediction table for the L1D prefetc filtering based on
    // POPET predictions.
    this->_pref_pfn_table = std::vector<uint64_t>(1024, 0);
}

cp::iprefetcher* cc::cache::prefetcher() { return this->_prefetcher; }

void cc::cache::init_cache(const std::string& config_file) {
    pt::ptree props;
    pt::read_json(config_file, props);

    this->_init_cache_impl(props);
}

void cc::cache::init_replacement_policy() {
    std::string replacement_path = "./replacements/" + _replacement_name,
                replacement_config_path =
                    "config/replacements/" + _replacement_name + ".json";

    this->_replacement_policy_callable =
        dll::import_alias<cr::ireplacementpolicy*()>(
            replacement_path, "create_replacementpolicy",
            dll::load_mode::append_decorations);

    this->_replacement_policy = this->_replacement_policy_callable();
    this->_replacement_policy->init(replacement_config_path, this);
}

cc::cache_type cc::cache::type() const {
    return static_cast<cc::cache_type>(this->_cache_type);
}

bool cc::cache::check_type(const cc::cache_type& type) const {
    return (this->_cache_type & type);
}

uint32_t& cc::cache::fill_level() { return this->_fill_level; }

const uint32_t& cc::cache::fill_level() const { return this->_fill_level; }

const uint64_t& cc::cache::pf_requested() const { return this->_pf_requested; }

const uint64_t& cc::cache::pf_issued() const { return this->_pf_issued; }

const uint64_t& cc::cache::pf_useful() const { return this->_pf_useful; }

const uint64_t& cc::cache::pf_useless() const { return this->_pf_useless; }

const uint64_t& cc::cache::pf_fill() const { return this->_pf_fill; }

/**
 * @brief This methods issues a prefetch in this cache instance. However, it
 * ensures that prefetches do not cross page boundaries.
 *
 * @param cpu The id of the CPU issuing the prefetch.
 * @param size The size of memory block to prefetch (usually the size of a cache
 * block).
 * @param ip The IP associated to the prefetch.
 * @param base_addr The base address of the prefetch.
 * @param pf_addr The address of the block to prefetch.
 * @param fill_level Which level of cache hierarchy to fill.
 * @param metadata Metadata associated to prefetch.
 *
 * @return Returns true if the prefetch has been successfully issued.
 */
bool cc::cache::prefetch_line(const uint32_t& cpu, const uint8_t& size,
                              const uint64_t& ip, const uint64_t& base_addr,
                              const uint64_t& pf_addr,
                              const cc::cache::fill_levels& fill_level,
                              const uint32_t& metadata) {
    bool cross_page_bounds =
             ((base_addr >> LOG2_PAGE_SIZE) != (pf_addr >> LOG2_PAGE_SIZE)),
         queue_full =
             (this->_prefetch_queue->occupancy == this->_prefetch_queue->SIZE);
    PACKET pf_packet;
    cc::locmap_prediction_descriptor pred_desc;
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(cpu);

    // Updating stats.
    this->_pf_requested++;

    if (cross_page_bounds || queue_full) {
        return false;  // No prefetch issued.
    }

    // Filling the prefetch packet.
    pf_packet.fill_level = fill_level;

    if (fill_level == fill_l1 || fill_level == fill_sdc) {
        pf_packet.fill_l1d = 1;
    }

    pf_packet.pf_origin_level = this->_fill_level;
    pf_packet.pf_metadata = metadata;
    pf_packet.cpu = cpu;
    pf_packet.address = (pf_addr >> LOG2_BLOCK_SIZE);
    pf_packet.full_addr = pf_addr;
    pf_packet.memory_size = size;
    pf_packet.ip = ip;
    pf_packet.type = cc::cache::prefetch;
    pf_packet.event_cycle = curr_cpu->current_core_cycle();
    pf_packet.went_offchip_pred = static_cast<bool>(
        metadata);  // WIP: Here the metadata is used to identify demand request
                    // predicted to be off-chip.

    // WIP: If we are trying to prefetch in the L1D, should the prefetcher be
    // enabled?
    if (this->check_type(cc::is_l1d)) {
        pred_desc = curr_cpu->_mm.predict_desc_on_prefetch(
            pf_packet, pf_packet.ip, pf_packet.full_addr, 0);

        // TODO: Perhards we should add a few latency cycles when the SSP is consulted?
#if defined(ENABLE_SSP)
        pf_packet.event_cycle += 0;
#endif // defined(ENABLE_SSP)
    }

    this->add_prefetch_queue(pf_packet);

    this->_pf_issued++;

    return true;  // Prefetch issued.
}

bool cc::cache::prefetcher_enable(const uint64_t& base_addr) const {
    uint64_t val = folded_xor(base_addr, 2);
    val = jenkins_hash(val) % this->_pref_pfn_table.size();

    return (this->_pref_pfn_table[val] <= this->_psel_threshold);
}

void cc::cache::set_prefetcher_psel_bits(const uint64_t& size) {
    this->_psel_max = (((1ULL) << size) - 1ULL);
    this->_psel_prefetching = this->_psel_max;
    this->_psel_threshold = this->_psel_max;
}

void cc::cache::set_prefetcher_threshold(const uint64_t& threshold) {
    assert(threshold <= this->_psel_max);

    this->_psel_threshold = threshold;
}

void cc::cache::increment_pref_pred(const uint64_t& base_addr) {
    uint64_t val = folded_xor(base_addr, 2);
    val = jenkins_hash(val) % this->_pref_pfn_table.size();

    if (this->_pref_pfn_table[val] < this->_psel_max)
        this->_pref_pfn_table[val]++;
}

void cc::cache::decrement_pref_pred(const uint64_t& base_addr) {
    int64_t val = folded_xor(base_addr, 2);
    val = jenkins_hash(val) % this->_pref_pfn_table.size();

    if (this->_pref_pfn_table[val] > 0) this->_pref_pfn_table[val]--;
}

int cc::cache::add_rq(PACKET* packet) { return this->add_read_queue(*packet); }

int cc::cache::add_wq(PACKET* packet) { return this->add_write_queue(*packet); }

int cc::cache::add_pq(PACKET* packet) {
    return this->add_prefetch_queue(*packet);
}

void cc::cache::return_data(PACKET* packet) { this->return_data(*packet); }

cc::cache::stats_vector& cc::cache::stats() { return this->_stats; }

const cc::cache::stats_vector& cc::cache::stats() const { return this->_stats; }

cc::cache::cache_stats& cc::cache::bypass_stats() {
    return this->_bypass_stats;
}

const cc::cache::cache_stats& cc::cache::bypass_stats() const {
    return this->_bypass_stats;
}

/**
 * @brief Return the MSHR container.
 *
 * @return const std::vector<PACKET>& A const-reference to the MSHR container.
 */
std::vector<PACKET>& cc::cache::mshr() { return this->_mshr; }

/**
 * @brief Return the MSHR container.
 *
 * @return const std::vector<PACKET>& A const-reference to the MSHR container.
 */
const std::vector<PACKET>& cc::cache::mshr() const { return this->_mshr; }

/**
 * @brief Tries to allocate a MSHR entry. This is a public interface to the
 * MSHRs that can be accessed by any other memory component.
 *
 * @param packet The packet to be added.
 * @return cc::cache::mshr_iterator An iterator to the newly added MSHR
 * entry.
 */
cc::cache::mshr_iterator cc::cache::allocate_mshr(PACKET& packet) {
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);
    mshr_iterator it = std::find_if(this->_mshr.begin(), this->_mshr.end(),
                                    [](const PACKET& e) -> bool {
                                        return (e.address == 0x0);
                                    }),
                  it_cpy;

    // Only allocate an MSHR if it is meant to fill this cache level. This is
    // useful on prefetch miss that should only fill lower levels. In this case
    // we return an invalid value whilst not throwing exceptions.
    if (packet.fill_level > this->_fill_level &&
        !(this->check_type(cc::is_llc) &&
          packet.fill_level == cc::cache::fill_metadata)) {
        return this->_mshr.end();
    }

    // We check if there is such a packet already allocated in the MSHR. If yes,
    // we simply stop here and return the iterator that matched.
    if ((it_cpy = this->find_mshr(packet)) != this->_mshr.end()) {
        this->merge_mshr_on_read(packet, it_cpy);

        return it_cpy;
    }

    if (it != this->_mshr.end()) {
        *it = packet;
        it->returned = INFLIGHT;
        it->cycle_enqueued = curr_cpu->current_core_cycle();
    } else {  // Attempt to allocate MSHR with no free sports. Throwing an
              // exception.
        throw std::logic_error("No free spot found in the MSHR.");
    }

/*
 * Sanity check: We shouldn't have more than one matching entry after insertion.
 * The count can not be exactly 0 as we have just inserted an element in the
 * MSHR. It can not be greater than 1 either as that would mean duplicates in
 * the MSHRs.
 */
#ifndef NDEBUG
    if (std::count_if(this->_mshr.begin(), this->_mshr.end(),
                      [&p = packet](const PACKET& e) -> bool {
                          bool same_cpu = (e.cpu == p.cpu);

                          return same_cpu && (e.address == p.address);
                      }) != 1)
        throw std::runtime_error("");
#endif  // NDEBUG

    // Sorting the MSHR entries.
    // this->_sort_mshr();

    // As we just sorted the MSHR entries, the iterator is likely to be invalid.
    // it = this->find_mshr(packet);

    return it;
}

/**
 * @brief Finds a MSHR entry that corresponds to the provided cache.
 *
 * @param packet The packet to look for in the MSHR.
 * @return cc::cache::mshr_iterator An iterator that points at the MSHR
 * entry found. In case no matching entry was found, this is _mshr.end().
 */
cc::cache::mshr_iterator cc::cache::find_mshr(const PACKET& packet) {
    return std::find_if(this->_mshr.begin(), this->_mshr.end(),
                        [packet](const PACKET& e) -> bool {
                            bool same_cpu = (e.cpu == packet.cpu);

                            return same_cpu && (e.address == packet.address);
                        });
}

/**
 * @brief Merges a packet with a existing MSHR entry. This is typically used
 * when on a miss a match is found in the MSHR of a cache and a merge is
 * required.
 *
 * @pre The packet `src` must refer to the same block as `dst`.
 *
 * @param src The packet to be merge in dst.
 * @param dst The packet in which src must be merged.
 */
void cc::cache::merge_mshr_on_read(PACKET& src, cc::cache::mshr_iterator dst) {
    bool modified = false;

    if (src.fill_level == cc::cache::fill_metadata ^
        dst->fill_level == cc::cache::fill_metadata) {
        std::cerr << std::hex << src.full_addr << " " << dst->full_addr
                  << std::dec << std::endl;
    }

    if (src.cpu != dst->cpu) {
        throw std::runtime_error("Invalide cpu index.");
    }

    // First, let's check the pre-condition. If not met, we cannot go further.
    if (src.address != dst->address) {
        // TODO: We should provide more details about both packets.
        throw std::runtime_error(
            "Merging packets referring to different memory blocks is not "
            "allowed.");
    }

    // Mark merged consumer.
    if (src.type == cc::cache::rfo) {
        if (src.tlb_access) {
            dst->store_merged = 1;
            dst->sq_index_depend_on_me.insert(src.sq_index);
            dst->sq_index_depend_on_me.join(src.sq_index_depend_on_me, SQ_SIZE);
        }

        if (src.load_merged) {
            dst->load_merged = 1;
            dst->lq_index_depend_on_me.join(src.lq_index_depend_on_me, LQ_SIZE);
        }
    } else {
        if (src.instruction) {
            dst->instruction = 1;
            dst->instr_merged = 1;
            dst->rob_index_depend_on_me.insert(src.rob_index);

            if (src.instr_merged) {
                dst->rob_index_depend_on_me.join(src.rob_index_depend_on_me,
                                                 ROB_SIZE);
            }
        } else {
            dst->data = 1;
            dst->load_merged = 1;
            dst->lq_index_depend_on_me.insert(src.lq_index);
            dst->lq_index_depend_on_me.join(src.lq_index_depend_on_me, LQ_SIZE);

            if (src.store_merged) {
                dst->store_merged = 1;
                dst->sq_index_depend_on_me.join(src.sq_index_depend_on_me,
                                                SQ_SIZE);
            }
        }
    }

    // Updating fill level.
    if (src.fill_level < dst->fill_level) {
        dst->fill_level = src.fill_level;
    }

    if (src.fill_level == cc::cache::fill_dclr ^
        dst->fill_level == cc::cache::fill_dclr) {
        dst->fill_level = (dst->fill_level < src.fill_level) ? dst->fill_level
                                                             : src.fill_level;
    }

    if (src.fill_l1i == 1 && dst->fill_l1i != 1) {
        dst->fill_l1i = 1;
    }

    if (src.fill_l1d == 1 && dst->fill_l1d != 1) {
        dst->fill_l1d = 1;
    }

    // TODO: Updating fill path.
    dst->merge_fill_path(src, &modified);

    if (!dst->fill_path.empty()) {
        dst->pop_fill_path_until(
            [tc = this](const PACKET::fill_path_t& fp) -> bool {
                return (*fp.top() < *tc || fp.top() == tc);
            });
    }

    if (dst->returned == COMPLETED && !dst->fill_path.empty() &&
        dst->fill_path.top() == this)
        dst->pop_fill_path();

    // Updating request.
    if (dst->type == cc::cache::prefetch) {
        cc::cache::fill_levels prior_fill_level =
            static_cast<cc::cache::fill_levels>(dst->fill_level);
        uint8_t prior_returned = dst->returned;
        uint32_t prior_pf_origin_level = dst->pf_origin_level;
        uint64_t prior_event_cycle = dst->event_cycle;
        cc::sdc_routes prior_route = dst->route;
        PACKET::fill_path_t prior_fill_path = dst->fill_path;
        cc::uarch_state_info prior_uarch_info = dst->info;
        float prior_perc_weights_sum = dst->perceptron_weights_sum;
        bool prior_pf_went_offchip = dst->pf_went_offchip;

        *dst = src;

        dst->returned = prior_returned;
        dst->event_cycle = prior_event_cycle;
        dst->route = prior_route;
        dst->pf_origin_level = prior_pf_origin_level;
        dst->fill_path = prior_fill_path;
        dst->fill_level = (prior_fill_level <= cc::cache::fill_llc)
                              ? prior_fill_level
                              : dst->fill_level;
        dst->info = prior_uarch_info;
        dst->perceptron_weights_sum = prior_perc_weights_sum;
        dst->pf_went_offchip = prior_pf_went_offchip;
    }

    // If the fill path has been modified we must tell the lower levels about
    // that.
    if (modified) {
        this->_update_fill_path_lower_mshrs(dst);
        uncore.DRAM.update_fill_path(*dst);
    }
}

void cc::cache::merge_mshr_on_writeback(PACKET& src,
                                        cc::cache::mshr_iterator dst) {
    bool modified = false;

    if (src.fill_level == cc::cache::fill_metadata ^
        dst->fill_level == cc::cache::fill_metadata) {
        std::cerr << std::hex << src.full_addr << " " << dst->full_addr
                  << std::dec << std::endl;
    }

    if (src.cpu != dst->cpu) {
        throw std::runtime_error("Invalide cpu index.");
    }

    // First, let's check the pre-condition. If not met, we cannot go further.
    if (src.address != dst->address) {
        // TODO: We should provide more details about both packets.
        throw std::runtime_error(
            "Merging packets referring to different memory blocks is not "
            "allowed.");
    }

    // Updating fill level.
    if (src.fill_level < dst->fill_level) {
        dst->fill_level = src.fill_level;
    }

    if (src.fill_l1i == 1 && dst->fill_l1i != 1) {
        dst->fill_l1i = 1;
    }

    if (src.fill_l1d == 1 && dst->fill_l1d != 1) {
        dst->fill_l1d = 1;
    }

    dst->merge_fill_path(src, &modified);

    // Updating request.
    if (dst->type == cc::cache::prefetch) {
        uint8_t prior_returned = dst->returned;
        uint32_t prior_pf_origin_level = dst->pf_origin_level;
        uint64_t prior_event_cycle = dst->event_cycle;
        cc::sdc_routes prior_route = dst->route;
        PACKET::fill_path_t prior_fill_path = dst->fill_path;
        cc::uarch_state_info prior_uarch_info = dst->info;
        float prior_perc_weights_sum = dst->perceptron_weights_sum;
        bool prior_pf_went_offchip = dst->pf_went_offchip;

        *dst = src;

        dst->returned = prior_returned;
        dst->event_cycle = prior_event_cycle;
        dst->pf_origin_level = prior_pf_origin_level;
        dst->fill_path = prior_fill_path;
        dst->info = prior_uarch_info;
        dst->perceptron_weights_sum = prior_perc_weights_sum;
        dst->pf_went_offchip = prior_pf_went_offchip;
    }

    // If the fill path has been modified we must tell the lower levels about
    // that.
    if (modified) {
        this->_update_fill_path_lower_mshrs(dst);
        uncore.DRAM.update_fill_path(*dst);
    }
}

void cc::cache::merge_mshr_on_prefetch(PACKET& src,
                                       cc::cache::mshr_iterator dst) {
    bool modified = false;

    if (src.fill_level == cc::cache::fill_metadata ^
        dst->fill_level == cc::cache::fill_metadata) {
        std::cerr << std::hex << src.full_addr << " " << dst->full_addr
                  << std::dec << std::endl;
    }

    if (src.cpu != dst->cpu) {
        throw std::runtime_error("Invalide cpu index.");
    }

    // First, let's check the pre-condition. If not met, we cannot go further.
    if (src.address != dst->address) {
        // TODO: We should provide more details about both packets.
        throw std::runtime_error(
            "Merging packets referring to different memory blocks is not "
            "allowed.");
    }

    // Updating fill level.
    if (src.fill_level < dst->fill_level) {
        dst->fill_level = src.fill_level;
    }

    if (src.fill_l1i == 1 && dst->fill_l1i != 1) {
        dst->fill_l1i = 1;
    }

    if (src.fill_l1d == 1 && dst->fill_l1d != 1) {
        dst->fill_l1d = 1;
    }

    dst->merge_fill_path(src, &modified);

    // TODO: Transmitting the info about the offchip prediction.
    if (!dst->pf_went_offchip_pred) {
        dst->pf_went_offchip_pred = src.pf_went_offchip_pred;
        dst->info = src.info;
        dst->perceptron_weights_sum = src.perceptron_weights_sum;
    }

    this->_update_pf_offchip_pred_lower(dst);

    // If the fill path has been modified we must tell the lower levels about
    // that.
    if (modified) {
        this->_update_fill_path_lower_mshrs(dst);
        uncore.DRAM.update_fill_path(*dst);
    }
}

/**
 * @brief Checks whether at least one MSHR is free for allocation.
 *
 * @return true All MSHRs are occupied.
 * @return false At least one MSHR is free for allocation.
 */
bool cc::cache::mshr_full() const {
    return std::none_of(
        this->_mshr.begin(), this->_mshr.end(),
        [](const PACKET& e) -> bool { return (e.address == 0x0); });
}

void cc::cache::reset_stats() {
    for (auto& e : this->_stats) {
        for (auto& [first, second] : e) {
            second.clear();
        }
    }

    this->_bypass_stats.clear();

    this->_total_miss_latency = 0;

    this->_pf_requested = 0ULL;
    this->_pf_issued = 0ULL;
    this->_pf_useful = 0ULL;
    this->_pf_useless = 0ULL;
    this->_pf_fill = 0ULL;

    this->_pf_useful_per_loc.clear();
    this->_pf_useless_per_loc.clear();

    // Resetting the internal stats of the prefetcher.
    this->_prefetcher->clear_stats();
}

void cc::cache::report(std::ostream& os, const size_t& cpu) {
    std::list<std::string> headers = {"ACCESS",    "HIT",      "MISS",
                                      "REG_HIT",   "REG_MISS", "IRREG_HIT",
                                      "IRREG_MISS"};
    std::size_t max_figure_len = 0, curr_figure_len;
    float avg_miss_latency = 0.0f;
    cc::cache::cache_stats cs, curr_cs;
    std::map<access_types, cache_stats> llc_cs = {
        {cc::cache::load, cc::cache::cache_stats()},
        {cc::cache::rfo, cc::cache::cache_stats()},
        {cc::cache::prefetch, cc::cache::cache_stats()},
        {cc::cache::writeback, cc::cache::cache_stats()},
    };
    bool is_llc = this->check_type(cc::is_llc);
    uint64_t curr_value = 0;

    // First, we have to check the lenght of the figure to print.
    for (const auto& e : {cc::cache::load, cc::cache::rfo, cc::cache::prefetch,
                          cc::cache::writeback}) {
        for (const uint64_t& f :
             {this->_stats[cpu][e].access, this->_stats[cpu][e].hit,
              this->_stats[cpu][e].miss}) {
            max_figure_len = ((curr_figure_len = std::to_string(f).length()) >
                              max_figure_len)
                                 ? curr_figure_len
                                 : max_figure_len;
        }
    }

    // Double checking with the headers.
    for (const auto& e : headers) {
        max_figure_len = ((curr_figure_len = e.length()) > max_figure_len)
                             ? curr_figure_len
                             : max_figure_len;
    }

    // If this is an LLC, we gather everything.
    if (is_llc) {
        for (const auto& e : {cc::cache::load, cc::cache::rfo,
                              cc::cache::prefetch, cc::cache::writeback}) {
            for (auto& cpu_stats : this->_stats) {
                llc_cs[e] += cpu_stats[e];
            }
        }
    }

    os << "[" << this->_name << " metrics]" << std::endl;

    // Printing table headers.
    os << std::setw(10) << ""
       << " ";
    for (const auto& e : headers) {
        os << std::setw(max_figure_len + 1) << std::left << e;
    }
    os << std::endl;

    for (const auto& e : {cc::cache::load, cc::cache::rfo, cc::cache::prefetch,
                          cc::cache::writeback}) {
        if (is_llc) {
            curr_cs = llc_cs[e];
        } else {
            curr_cs = this->_stats[cpu][e];
        }

        os << std::setw(10) << std::left << access_type_map[e].second << " "
           << std::setw(max_figure_len + 1) << std::left << curr_cs
           << std::endl;

        cs += curr_cs;
    }

    os << std::setw(10) << std::left << "TOTAL"
       << " " << std::setw(max_figure_len + 1) << std::left << cs << std::endl
       << std::endl;

    avg_miss_latency = static_cast<float>(this->_total_miss_latency) /
                       static_cast<float>(cs.miss);

    os << "Average miss latency: " << avg_miss_latency << std::endl
       << std::endl;

    os << "pf_requested: " << this->_pf_requested
       << " pf_issued: " << this->_pf_issued
       << " pf_useless: " << this->_pf_useless
       << " pf_useful: " << this->_pf_useful << " pf_fill: " << this->_pf_fill
       << std::endl
       << std::endl;

    // WIP: If this is an L1D we print stats prefetching accuracy per location.
    if (this->check_type(cc::is_l1d)) {
        // First, the useful prefetches.
        os << "Useful prefetches L2C: " << this->_pf_useful_per_loc[cc::is_l2c]
           << " LLC: " << this->_pf_useful_per_loc[cc::is_llc]
           << " DRAM: " << this->_pf_useful_per_loc[cc::is_dram]
           << std::endl;
        // Second the useless prefetches.
        os << "Useless prefetches L2C: " << this->_pf_useless_per_loc[cc::is_l2c]
           << " LLC: " << this->_pf_useless_per_loc[cc::is_llc]
           << " DRAM: " << this->_pf_useless_per_loc[cc::is_dram]
           << std::endl
           << std::endl;
    }

    this->_prefetcher->dump_stats();
}

/**
 * @brief This method is actually operate the cache at a high-level. It specify
 * the total numbers of reads and writes available at each cycle that must be
 * used among fills, write-backs, reads and prefetches.
 *
 * @todo In order to refactor the cache implementation in a cleaner fashion, the
 * _handle_* methods could be implemented using recursion. That would avoid
 * resetting counters each cycle.
 */
void cc::cache::operate() {
    // Performs all writes.
    this->_writes_avail_cycle = this->_writes_avail;

    this->_handle_fill();
    this->_handle_writeback();

    // Performs all reads.
    this->_reads_avail_cycle = this->_reads_avail;

    this->_handle_read();

    if (this->_prefetch_queue->occupancy && this->_reads_avail_cycle > 0) {
        this->_handle_prefetch();
    }
}

void cc::cache::increment_WQ_FULL(uint64_t address) {}

uint32_t cc::cache::get_occupancy(uint8_t queue_type, uint64_t address) {
    return 0;
}

uint32_t cc::cache::get_size(uint8_t queue_type, uint64_t address) { return 0; }

/**
 * @brief Adds a packet to the read queue of the cache. This method first checks
 * if the provided packet is already present in either the write queue or the
 * read. In case it is, the packet is either merged or data are reaturned.
 * @param packet A packet corresponding to a memory access that is intended to
 * be added to the read queue.
 * @return Returns add_queue_success in case the addition to the queue was
 * successful. In case of failure, returns add_queue_failure. In case of a
 * merge, returns the index in which the packet was merged.
 */
int32_t cc::cache::add_read_queue(PACKET& packet) {
    bool return_data_to_core = (packet.fill_level != cc::cache::fill_dclr);
    std::size_t rq_index, wq_index, sq_index, lq_index;

    // Sanity check (all packet MUST have a route and a fill path when arriving
    // in the L2C.)
    if (this->check_type(cc::is_l2c) && return_data_to_core) {
        assert(!packet.fill_path.empty());
    }

    // Checking if the packet is already present in the write queue of this
    // cache.
    if ((wq_index = this->_write_queue->check_queue(
             this, packet, check_duplicate_block_with_route)) != -1) {
        // WIP: using the fill path stack to direct the block.
        if (!packet.fill_path.empty()) {
            packet.data = (*this->_write_queue)[wq_index].data;

            // If the fill path is not empty, we carry on.
            packet.pop_fill_path()->return_data(packet);
        }

        // Sanity checks. Legacy from the original ChampSim. If this cache is
        // either a ITBL, a DTLB or a STLB, this bit-wise expression will
        // evaluate to a non-null value
        // TODO: CHange this.
        assert(this->_cache_type != cc::is_itlb &&
               this->_cache_type != cc::is_dtlb &&
               this->_cache_type != cc::is_stlb);

        // Updating processed packets.
        if ((this->_cache_type == is_l1d || this->_cache_type == cc::is_sdc) &&
            packet.type != prefetch) {
            if (!this->_processed->is_full()) {
                this->_processed->add_queue(packet);
            } else {
            }
        }

        // Updating write queue stats.
        this->_write_queue->FORWARD++;
        this->_write_queue->ACCESS++;

        return cc::cache::add_queue_success;
    }

    // Checking if the packet is already present in the read queue of this
    // cache.
    if ((rq_index = this->_read_queue->check_queue(
             this, packet, check_duplicate_block_with_route)) != -1) {
        PACKET& rq_entry = this->_read_queue->entry[rq_index];

        if (this->_read_queue->entry[rq_index].cpu != packet.cpu) {
            throw std::runtime_error("Invalide cpu index.");
        }

        if (packet.instruction) {
            rq_entry.rob_index_depend_on_me.insert(packet.rob_index);
            rq_entry.instruction = 1;
            rq_entry.instr_merged = 1;
        } else {
            // Mark merged consumer.
            if (packet.type == RFO) {
                sq_index = packet.sq_index;

                rq_entry.sq_index_depend_on_me.insert(sq_index);
                rq_entry.store_merged = 1;
            } else {
                lq_index = packet.lq_index;

                rq_entry.lq_index_depend_on_me.insert(lq_index);
                rq_entry.load_merged = 1;
            }

            rq_entry.is_data = 1;
        }

        // Updating fill parameters of the merged packet.
        if (packet.fill_l1i == 1 && rq_entry.fill_l1i != 1) {
            rq_entry.fill_l1i = 1;
        }

        if (packet.fill_l1d == 1 && rq_entry.fill_l1d != 1) {
            rq_entry.fill_l1d = 1;
        }

#if defined(ENABLE_DCLR)
        if (rq_entry.fill_level == cc::cache::fill_dclr) {
            if (packet.fill_level <= cc::cache::fill_llc) {
                /**
                 * DCLR: Here, there's already a DCLR request hanging in the
                 * cache's queue and the incoming request comes from the core's
                 * cache hierarchy.
                 */
                uint8_t tmp_scheduled = rq_entry.scheduled;
                uint64_t tmp_event_cycle = rq_entry.event_cycle,
                         tmp_cycle_enqueued = rq_entry.cycle_enqueued;

                // Merge.
                rq_entry = packet;

                // Update.
                rq_entry.scheduled = tmp_scheduled;
                rq_entry.event_cycle = tmp_event_cycle;
                rq_entry.cycle_enqueued = tmp_cycle_enqueued;
            } else if (packet.fill_level == cc::cache::fill_dclr) {
                /**
                 * DCLR: Here there's nothing to do as this is a DCLR request
                 * hitting another DCLR request in the cache's RQ.
                 */
                // stats.
            } else {
                throw std::runtime_error("");
                return -2;
            }
        }
#endif  // defined(ENABLE_DCLR)

        // WIP: We probably also need to merge the fill paths as they may
        // differ.
        (*this->_read_queue)[rq_index].merge_fill_path(packet);

        // Updating the queue stat counters.
        this->_read_queue->MERGED++;
        this->_read_queue->ACCESS++;

        return static_cast<cc::cache::add_queue_codes>(rq_index);
    }

    /*
     * At this point, we have no other choice than inserting the packet in the
     * read queue. But first, we need to check if there is enough space in the
     * queue to do so.
     */
    if (this->_read_queue->is_full()) {
        this->_read_queue->mark_full();

        return cc::cache::add_queue_failure;
    }

    // There is enough space, so we can add the packet to the queue.
    uint32_t latency = this->_latency;
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);
    latency +=
        (this->_cache_type == cc::is_l1d || this->_cache_type == cc::is_sdc)
            ? curr_cpu->irreg_pred.latency()
            : 0;

    this->_read_queue->add_queue(packet, latency);

    return cc::cache::add_queue_success;
}

/**
 * @brief Adds a packet to the write queue of the cache.
 * @param packet A packet corresponding to a memory access that is intended to
 * be added to the read queue.
 * @return Returns add_queue_success in case the addition to the queue was
 * successful. In case of failure, returns add_queue_failure. In case of a
 * merge, returns the index in which the packet was merged.
 */
int32_t cc::cache::add_write_queue(PACKET& packet) {
    std::size_t wq_index;

    // Checking if the packet is already present in the write queue of this
    // cache.
    if ((wq_index = this->_write_queue->check_queue(
             this, packet, check_duplicate_block_with_route)) != -1) {
        if (this->_write_queue->entry[wq_index].cpu != packet.cpu) {
            throw std::runtime_error("Invalide cpu index.");
        }

        // WIP: We probably also need to merge the fill paths as they may
        // differ.
        (*this->_write_queue)[wq_index].merge_fill_path(packet);

        // if ((wq_index = this->_write_queue->check_queue (&packet)) != -1) {
        this->_write_queue->MERGED++;
        this->_write_queue->ACCESS++;

        // Returning the index of the write queue entry in which this packet
        // is merged.
        return static_cast<cc::cache::add_queue_codes>(wq_index);
    }

    // Sanity checks.
    if (this->_write_queue->is_full()) {
        // TODO: Throw an exception.
    }

    // In case there is no duplicates, we add the packet to the write queue.
    wq_index = this->_write_queue->tail;

    if ((*this->_write_queue)[wq_index].address != 0x0) {
        // TODO: throw an exception.
    }

    uint32_t latency = this->_latency;
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);
    latency +=
        (this->_cache_type == cc::is_l1d || this->_cache_type == cc::is_sdc)
            ? curr_cpu->irreg_pred.latency()
            : 0;

    this->_write_queue->add_queue(packet, latency);

    // Updating the write queue stats counters.
    this->_write_queue->TO_CACHE++;
    this->_write_queue->ACCESS++;

    return cc::cache::add_queue_success;
}

/**
 * @brief Adds a packet to the prefetch queue of the cache.
 * @param packet A packet corresponding to a memory access that is intended to
 * be added to the read queue.
 * @return Returns add_queue_success in case the addition to the queue was
 * successful. In case of failure, returns add_queue_failure. In case of a
 * merge, returns the index in which the packet was merged.
 */
int32_t cc::cache::add_prefetch_queue(PACKET& packet) {
    std::size_t wq_index, pq_index;

    // Checking if the packet is already present in the write queue of this
    // cache.
    if ((wq_index = this->_write_queue->check_queue(
             this, packet, check_duplicate_block_with_route)) != -1) {
        // WIP: using the fill path stack to direct the block.
        if (!packet.fill_path.empty()) {
            packet.data = (*this->_write_queue)[wq_index].data;

            // If the fill path is not empty, we carry on.
            packet.pop_fill_path()->return_data(packet);
        }

        this->_write_queue->FORWARD++;
        this->_write_queue->ACCESS++;

        return cc::cache::add_queue_success;
    }

    // Checking if the packet is already present in the prefetch queue of this
    // cache.
    if ((pq_index = this->_prefetch_queue->check_queue(
             this, packet, check_duplicate_block_with_route)) != -1) {
        PACKET& pq_packet = this->_prefetch_queue->entry[pq_index];

        if (this->_prefetch_queue->entry[pq_index].cpu != packet.cpu) {
            throw std::runtime_error("Invalide cpu index.");
        }

        if (packet.fill_level < (*this->_prefetch_queue)[pq_index].fill_level) {
            (*this->_prefetch_queue)[pq_index].fill_level = packet.fill_level;
        }

        if (packet.instruction == 1 &&
            (*this->_prefetch_queue)[pq_index].instruction != 1) {
            (*this->_prefetch_queue)[pq_index].instruction = 1;
        }

        if (packet.is_data == 1 &&
            (*this->_prefetch_queue)[pq_index].is_data != 1) {
            (*this->_prefetch_queue)[pq_index].is_data = 1;
        }

        if (packet.fill_l1i == 1 &&
            (*this->_prefetch_queue)[pq_index].fill_l1i != 1) {
            (*this->_prefetch_queue)[pq_index].fill_l1i = 1;
        }

        if (packet.fill_l1d == 1 &&
            (*this->_prefetch_queue)[pq_index].fill_l1d != 1) {
            (*this->_prefetch_queue)[pq_index].fill_l1d = 1;
        }

        // TODO: Transmitting the info about the offchip prediction.
        if (!pq_packet.pf_went_offchip_pred) {
            pq_packet.pf_went_offchip_pred = packet.pf_went_offchip_pred;
            pq_packet.info = packet.info;
            pq_packet.perceptron_weights_sum = packet.perceptron_weights_sum;
        }

        // WIP: We probably also need to merge the fill paths as they may
        // differ.
        (*this->_prefetch_queue)[pq_index].merge_fill_path(packet);

        this->_prefetch_queue->MERGED++;
        this->_prefetch_queue->ACCESS++;

        return static_cast<cc::cache::add_queue_codes>(pq_index);
    }

    // Check occupancy.
    if (this->_prefetch_queue->occupancy == this->_prefetch_queue->SIZE) {
        this->_prefetch_queue->FULL++;

        return cc::cache::add_queue_failure;
    }

    // Adding the packet to the prefetch queue.
    uint32_t latency = this->_latency;
    O3_CPU* curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);
    latency +=
        (this->_cache_type == cc::is_l1d || this->_cache_type == cc::is_sdc)
            ? curr_cpu->irreg_pred.latency()
            : 0;

    this->_prefetch_queue->add_queue(packet, latency);

    // Sanity checks.
    if (packet.address == 0x0) {
        // TODO: Throw an exception.
    }

    this->_prefetch_queue->TO_CACHE++;
    this->_prefetch_queue->ACCESS++;

    return cc::cache::add_queue_success;
}

cc::cache::~cache() {
    // Freeing memory.
    delete this->_write_queue;
    delete this->_read_queue;
    delete this->_prefetch_queue;
    delete this->_processed;
}

uint32_t cc::cache::read_queue_occupancy() {
    return this->_read_queue->occupancy;
}

uint32_t cc::cache::read_queue_occupancy(const uint64_t& addr) {
    return this->read_queue_occupancy();
}

uint32_t cc::cache::write_queue_occupancy() {
    return this->_write_queue->occupancy;
}

uint32_t cc::cache::write_queue_occupancy(const uint64_t& addr) {
    return this->write_queue_occupancy();
}

uint32_t cc::cache::prefetch_queue_occupancy() {
    return this->_prefetch_queue->occupancy;
}

uint32_t cc::cache::prefetch_queue_occupancy(const uint64_t& addr) {
    return this->prefetch_queue_occupancy();
}

uint32_t cc::cache::read_queue_size() { return this->_read_queue->SIZE; }

uint32_t cc::cache::read_queue_size(const uint64_t& addr) {
    return this->read_queue_size();
}

uint32_t cc::cache::write_queue_size() { return this->_write_queue->SIZE; }

uint32_t cc::cache::write_queue_size(const uint64_t& addr) {
    return this->write_queue_size();
}

uint32_t cc::cache::prefetch_queue_size() {
    return this->_prefetch_queue->SIZE;
}

uint32_t cc::cache::prefetch_queue_size(const uint64_t& addr) {
    return this->prefetch_queue_size();
}

std::size_t cc::cache::mshr_size() const { return this->_mshr.size(); }

std::size_t cc::cache::mshr_occupancy() const {
    return std::count_if(
        this->_mshr.begin(), this->_mshr.end(),
        [](const auto& e) -> bool { return (e.address == 0ULL); });
}

uint32_t cc::cache::block_size() const { return BLOCK_SIZE; }

uint32_t cc::cache::log2_block_size() const {
    return static_cast<uint32_t>(std::log2f(this->block_size()));
}

PACKET_QUEUE* cc::cache::read_queue() const { return this->_read_queue; }

PACKET_QUEUE* cc::cache::write_queue() const { return this->_write_queue; }

PACKET_QUEUE* cc::cache::prefetch_queue() const {
    return this->_prefetch_queue;
}

PACKET_QUEUE* cc::cache::processed_queue() const { return this->_processed; }

void cc::cache::_init_cache_impl(const pt::ptree& props) {
    std::size_t write_queue_size, read_queue_size, prefetch_queue_size,
        mshr_size, processed_queue_size;
    std::string fill_level, cache_type, prefetcher_name, prefetcher_path,
        prefetcher_config_path;
    std::map<std::string, cc::cache_type>::iterator cache_type_it;
    std::map<std::string, cc::cache::fill_levels>::iterator fill_level_it;

    // Getting this instance's given name.
    this->_name = props.get<std::string>("name");

    this->_latency = props.get<uint32_t>("latency");

    cache_type = props.get<std::string>("cache_type");

    if ((cache_type_it = cache_type_map.find(cache_type)) ==
        cache_type_map.end()) {
        // TODO: Throw exception.
    }

    // Looking for all the possible cache types in the provided disjunction.
    // for (const auto& e: cache_type_map) {
    // 	if (std::search (cache_type.begin (), cache_type.end (), e.first.begin
    // (), e.first.end ()) != cache_type.end ()) { this->_cache_type
    // |= e.second;
    // 	}
    // }

    fill_level = props.get<std::string>("fill_level");

    if ((fill_level_it = fill_level_map.find(fill_level)) ==
        fill_level_map.end()) {
        // TODO: Throw exception.
    }

    this->_reads_avail = props.get<uint32_t>("max_reads");
    this->_writes_avail = props.get<uint32_t>("max_writes");

    this->_cache_type = cache_type_it->second;
    this->_fill_level = fill_level_it->second;

    // Getting knobs from config file.
    write_queue_size = props.get<std::size_t>("write_queue.size");
    read_queue_size = props.get<std::size_t>("read_queue.size");
    prefetch_queue_size = props.get<std::size_t>("prefetch_queue.size");
    mshr_size = props.get<std::size_t>("mshr.size");
    processed_queue_size = props.get<std::size_t>("processed_queue.size");

    // Intiializing what we can initialize at this point. Namely: write queue,
    // read queue, prefetch queue, mshr and last but not least, the processed
    // misses queue.
    this->_write_queue =
        new PACKET_QUEUE(this->_name + "_WQ", write_queue_size);
    this->_read_queue = new PACKET_QUEUE(this->_name + "_RQ", read_queue_size);
    this->_prefetch_queue =
        new PACKET_QUEUE(this->_name + "_PQ", prefetch_queue_size);
    this->_processed =
        new PACKET_QUEUE(this->_name + "_PROCESS", processed_queue_size);
    this->_mshr = std::vector<PACKET>(mshr_size);

    // Initializing the prefetcher.
    prefetcher_name = props.get<std::string>("prefetcher");
    prefetcher_path = "./prefetchers/" + prefetcher_name;
    prefetcher_config_path = "config/prefetchers/" + prefetcher_name + ".json";

    this->_prefetcher_callable = dll::import_alias<cp::iprefetcher*()>(
        prefetcher_path, "create_prefetcher",
        dll::load_mode::append_decorations);

    this->_prefetcher = this->_prefetcher_callable();
    this->_prefetcher->init(prefetcher_config_path, this);

    // Initializing the replacement policy.
    this->_replacement_name = props.get<std::string>("replacement_policy");

    // If this is an SDC, we initialize the routing engine.
    if (this->check_type(cc::is_sdc)) {
        this->_re.init(props.get_child("routing_engine"));
    }
}

/**
 * @brief Sorts entries of the MSHR such that the ones at the head are the first
 * to be consumed.
 *
 */
void cc::cache::_sort_mshr() {
    // Sorting the MSHR entries.
    std::sort(
        this->_mshr.begin(), this->_mshr.end(),
        [](const PACKET& a, const PACKET& b) -> bool {
            return (b.returned != COMPLETED ||
                    (a.returned == COMPLETED && a.event_cycle < b.event_cycle));
        });
}

void cc::cache::_l1i_prefetcher_cache_operate() {}

/**
 * @brief Tries to request a prefetch in the L1D cache.
 * @param cpu The index of the CPU requesting the prefetch.
 * @param addr The address from which the prefetch should be based on.
 * @param ip The instruction pointer associated to the prefetch request.
 * @param The size of the prefetch request (usually a cache line).
 * @param hit Was the access that triggered the prefetch a hit?
 * @param type What kind of access was the one that triggered the prefetch
 * request?
 */
void cc::cache::_l1d_prefetcher_operate(const uint32_t& cpu,
                                        const uint64_t& addr,
                                        const uint64_t& ip,
                                        const uint32_t& size, bool hit,
                                        const access_types& type) {
    uint64_t pf_addr = ((addr >> LOG2_BLOCK_SIZE) + 1) << LOG2_BLOCK_SIZE;

    return;

    // Requestung a prefetching of the immediate next cache block.
    this->prefetch_line(cpu, size, ip, addr, pf_addr, fill_l1, 0);
}

/**
 * @brief Tries to request a prefetch in the L2C cache.
 * @param cpu The index of the CPU requesting the prefetch.
 * @param addr The address from which the prefetch should be based on.
 * @param ip The instruction pointer associated to the prefetch request.
 * @param The size of the prefetch request (usually a cache line).
 * @param hit Was the access that triggered the prefetch a hit?
 * @param type What kind of access was the one that triggered the prefetch
 * request?
 */
void cc::cache::_l2c_prefetcher_operate(const uint32_t& cpu,
                                        const uint64_t& addr,
                                        const uint64_t& ip,
                                        const uint32_t& size, bool hit,
                                        const access_types& type) {
    // cp::prefetch_request_descriptor desc = {
    // 	// .hit = hit,
    // 	// .access_type = type,
    // 	// .cpu = cpu,
    // 	// .size = size,
    // 	// .addr = addr,
    // 	// .ip = ip,
    // };

    // this->_prefetcher->operate (desc);
}

/**
 * @brief Tries to request a prefetch in the LLC cache.
 * @param cpu The index of the CPU requesting the prefetch.
 * @param addr The address from which the prefetch should be based on.
 * @param ip The instruction pointer associated to the prefetch request.
 * @param The size of the prefetch request (usually a cache line).
 * @param hit Was the access that triggered the prefetch a hit?
 * @param type What kind of access was the one that triggered the prefetch
 * request?
 */
void cc::cache::_llc_prefetcher_operate(const uint32_t& cpu,
                                        const uint64_t& addr,
                                        const uint64_t& ip,
                                        const uint32_t& size, bool hit,
                                        const access_types& type) {
    uint64_t pf_addr = ((addr >> LOG2_BLOCK_SIZE) + 1) << LOG2_BLOCK_SIZE;

    return;

    // Requestung a prefetching of the immediate next cache block.
    this->prefetch_line(cpu, size, ip, addr, pf_addr, fill_llc, 0);
}

/**
 * @brief Tries to request a prefetch in the SDC cache.
 * @param cpu The index of the CPU requesting the prefetch.
 * @param addr The address from which the prefetch should be based on.
 * @param ip The instruction pointer associated to the prefetch request.
 * @param The size of the prefetch request (usually a cache line).
 * @param hit Was the access that triggered the prefetch a hit?
 * @param type What kind of access was the one that triggered the prefetch
 * request?
 */
void cc::cache::_sdc_prefetcher_operate(const uint32_t& cpu,
                                        const uint64_t& addr,
                                        const uint64_t& ip,
                                        const uint32_t& size, bool hit,
                                        const access_types& type) {
    uint64_t pf_addr = ((addr >> LOG2_BLOCK_SIZE) + 1) << LOG2_BLOCK_SIZE;

    return;

    // Requestung a prefetching of the immediate next cache block.
    this->prefetch_line(cpu, size, ip, addr, pf_addr, fill_sdc, 0);
}

void cc::cache::_account_non_aligned(const bool& aligned,
                                     uint64_t& non_aligned_cnt,
                                     uint64_t& received_cnt) {
    if (!aligned) {
        non_aligned_cnt++;
    }

    received_cnt++;
}

cc::cache::fill_levels cc::next_fill_level(const cc::cache::fill_levels& fl) {
    if (fl == cache::fill_sdc) {
        return cache::fill_dram;
    }

    return (fl != cache::fill_dram ? static_cast<cache::fill_levels>(fl << 1)
                                   : fl);
}

cc::cache::fill_levels cc::prev_fill_level(const cc::block_location& loc) {
    switch (loc) {
        case cc::is_in_l2c:
        case cc::is_in_both:
            return cc::cache::fill_l1;
            break;

        case cc::is_in_llc:
            return cc::cache::fill_l2;
            break;

        case cc::is_in_dram:
            return cc::cache::fill_llc;
            break;

        default:
            break;
    }
}

bool operator<(const cc::cache& lhs, const cc::cache& rhs) {
    return lhs.fill_level() < rhs.fill_level();
}

bool operator>(const cc::cache& lhs, const cc::cache& rhs) {
    return lhs.fill_level() > rhs.fill_level();
}

std::ostream& operator<<(std::ostream& os,
                         const champsim::components::cache& c) {
    return os;
}

std::ostream& operator<<(std::ostream& os, const cc::cache::cache_stats& cs) {
    os << cs.access << " " << cs.hit << " " << cs.miss << " " << cs.reg_data_hit
       << " " << cs.reg_data_miss << " " << cs.irreg_data_hit << " "
       << cs.irreg_data_miss;

    return os;
}
