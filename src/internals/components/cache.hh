#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_CACHE_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_CACHE_HH__

#include <cstdint>
#
#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>
#
#include <boost/dynamic_bitset.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/shared_ptr.hpp>
#
#include <internals/block.h>
#include <internals/memory_class.h>
#
#include <internals/components/memory_system.hh>
#include <internals/components/routing_engine.hh>

// Forward declarations.
namespace champsim {
namespace prefetchers {
class iprefetcher;
}

namespace replacements {
class ireplacementpolicy;
}
}  // namespace champsim

namespace cp = champsim::prefetchers;
namespace cr = champsim::replacements;
namespace pt = boost::property_tree;

namespace champsim {
namespace helpers {
struct cache_access_descriptor {
   public:
    bool hit, is_data;

    uint8_t cpu;
    uint8_t way;
    uint16_t set;

    uint64_t pc;

    uint64_t full_addr, full_vaddr, victim_addr;
    uint64_t lq_index;

    uint8_t type;

    // boost::dynamic_bitset<> footprint;
    std::size_t requested_size, memory_size;

    std::map<uint64_t, uint64_t>* inverse_table;

   public:
    cache_access_descriptor() {}
    //  : footprint(1, 0ULL) {}

    cache_access_descriptor(const std::size_t& footptint_size) {}
    // : footprint(footptint_size, 0ULL) {}

    ~cache_access_descriptor() {}
};
}  // namespace helpers

namespace components {
/**
 *
 */
// TODO: Remove the inheritance from ChampSim's MEMORY class.
class cache : public memory_system, public MEMORY {
   public:
    enum cache_layout {
        sectored_cache_layout = 0,
        distill_cache_layout = 1,
    };

    enum access_types {
        load = 0,
        rfo = 1,
        prefetch = 2,
        writeback = 3,
    };

    enum fill_levels : int32_t {
        fill_l1 = 1,
        fill_l2 = 2,
        fill_llc = 4,
        fill_drc = 8,
        fill_dram = 16,
        fill_metadata = 32,
        fill_ddrp = 64,
        fill_dclr = 128,

        fill_sdc = 0,
    };

    enum add_queue_codes : int32_t {
        add_queue_success = -1,
        add_queue_failure = -2,
    };

    struct prefetch_descriptor {
       public:
        uint8_t cpu, size;

        uint32_t metadata;

        uint64_t ip, base_addr, pf_addr;

        fill_levels fill_level;
    };

    struct cache_stats {
        uint64_t access;
        uint64_t hit, miss;
        uint64_t loc_hit, woc_hit, hole_miss, line_miss;

        uint64_t reg_data_hit, reg_data_miss, irreg_data_hit, irreg_data_miss;
        uint64_t bypass_pred, no_bypass_pred;

        uint64_t pf_requested, pf_issued, pf_useful, pf_useless, pf_fill;

        cache_stats()
            : access(0),
              hit(0),
              miss(0),
              loc_hit(0),
              woc_hit(0),
              hole_miss(0),
              line_miss(0),
              reg_data_hit(0),
              reg_data_miss(0),
              irreg_data_hit(0),
              irreg_data_miss(0),
              bypass_pred(0),
              no_bypass_pred(0),
              pf_requested(0),
              pf_issued(0),
              pf_useful(0),
              pf_useless(0),
              pf_fill(0) {}

        void clear() {
            std::tie(this->access, this->hit, this->miss, this->loc_hit,
                     this->woc_hit, this->hole_miss, this->line_miss,
                     this->reg_data_hit, this->reg_data_miss,
                     this->irreg_data_hit, this->irreg_data_miss,
                     this->bypass_pred, this->no_bypass_pred,
                     this->pf_requested, this->pf_issued, this->pf_useful,
                     this->pf_useless, this->pf_fill) =
                std::make_tuple(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0);
        }

        cache_stats operator+(const cache_stats& o) const {
            cache_stats r;

            r.access = this->access + o.access;
            r.hit = this->hit + o.hit;
            r.miss = this->miss + o.miss;
            r.loc_hit = this->loc_hit + o.loc_hit;
            r.woc_hit = this->woc_hit + o.woc_hit;
            r.hole_miss = this->hole_miss + o.hole_miss;
            r.line_miss = this->line_miss + o.line_miss;
            r.reg_data_hit = this->reg_data_hit + o.reg_data_hit;
            r.reg_data_miss = this->reg_data_miss + o.reg_data_miss;
            r.irreg_data_hit = this->irreg_data_hit + o.irreg_data_hit;
            r.irreg_data_miss = this->irreg_data_miss + o.irreg_data_miss;
            r.bypass_pred = this->bypass_pred + o.bypass_pred;
            r.no_bypass_pred = this->no_bypass_pred + o.no_bypass_pred;
            r.pf_requested = this->pf_requested + o.pf_requested;
            r.pf_issued = this->pf_issued + o.pf_issued;
            r.pf_useful = this->pf_useful + o.pf_useful;
            r.pf_useless = this->pf_useless + o.pf_useless;
            r.pf_fill = this->pf_fill + o.pf_fill;

            return r;
        }

        cache_stats& operator+=(const cache_stats& o) {
            this->access += o.access;
            this->hit += o.hit;
            this->miss += o.miss;
            this->loc_hit += o.loc_hit;
            this->woc_hit += o.woc_hit;
            this->hole_miss += o.hole_miss;
            this->line_miss += o.line_miss;
            this->reg_data_hit += o.reg_data_hit;
            this->reg_data_miss += o.reg_data_miss;
            this->irreg_data_hit += o.irreg_data_hit;
            this->irreg_data_miss += o.irreg_data_miss;
            this->bypass_pred += o.bypass_pred;
            this->no_bypass_pred += o.no_bypass_pred;
            this->pf_requested += o.pf_requested;
            this->pf_issued += o.pf_issued;
            this->pf_useful += o.pf_useful;
            this->pf_useless += o.pf_useless;
            this->pf_fill += o.pf_fill;

            return *this;
        }
    };

    using stats_vector = std::vector<std::map<access_types, cache_stats>>;

    using footprint_bitmap = boost::dynamic_bitset<>;
    using footprint_bitmap_set_array = std::vector<footprint_bitmap>;
    using footprint_bitmap_array = std::vector<footprint_bitmap_set_array>;

    using mshr_entries = std::vector<PACKET>;
    using mshr_iterator = mshr_entries::iterator;

    using block_usage = std::map<uint64_t, uint64_t>;

   protected:
    uint64_t _cpu;
    std::string _name, _replacement_name;

    uint32_t _reads_avail, _writes_avail, _reads_avail_cycle,
        _writes_avail_cycle;
    uint32_t _latency;
    uint32_t _fill_level;
    uint16_t _cache_type;
    PACKET_QUEUE *_read_queue, *_write_queue, *_prefetch_queue, *_processed;
    std::vector<PACKET> _mshr;

    // Prefetching stats.
    uint64_t _pf_requested, _pf_issued, _pf_useful, _pf_useless, _pf_fill;
    std::map<cc::cache_type, uint64_t> _pf_useful_per_loc, _pf_useless_per_loc;

    uint64_t _read_received, _read_overlap, _write_received, _write_overlap,
        _prefetch_received, _prefetch_overlap;

    uint64_t _total_miss_latency;

    uint64_t _psel_prefetching, _psel_threshold, _psel_max;
    std::vector<uint64_t> _pref_pfn_table;

    stats_vector _stats;
    cache_stats _bypass_stats;
    block_usage _block_usages;

    footprint_bitmap_array _footprints;

    cp::iprefetcher* _prefetcher;
    std::function<cp::iprefetcher*()> _prefetcher_callable;

    cr::ireplacementpolicy* _replacement_policy;
    std::function<cr::ireplacementpolicy*()> _replacement_policy_callable;

    routing_engine _re;

   public:
    cache();
    virtual ~cache();

    cp::iprefetcher* prefetcher();

    void init_cache(const std::string& config_file);
    void init_replacement_policy();

    const uint64_t& cpu() const { return this->_cpu; }

    cache_type type() const;
    bool check_type(const cache_type& type) const;

    uint32_t& fill_level();
    const uint32_t& fill_level() const;

    virtual cache_layout layout() const = 0;

    virtual std::size_t sets() const = 0;
    virtual std::size_t associativity() const = 0;

    const uint64_t& pf_requested() const;
    const uint64_t& pf_issued() const;
    const uint64_t& pf_useful() const;
    const uint64_t& pf_useless() const;
    const uint64_t& pf_fill() const;

    // Interfaces for stats.
    stats_vector& stats();
    const stats_vector& stats() const;

    cache_stats& bypass_stats();
    const cache_stats& bypass_stats() const;

    // Interface regarding the allocation & deallocation of MSHRs.
    std::vector<PACKET>& mshr();
    const std::vector<PACKET>& mshr() const;
    mshr_iterator allocate_mshr(PACKET& packet);
    mshr_iterator find_mshr(const PACKET& packet);
    void merge_mshr_on_read(PACKET& src, mshr_iterator dst),
        merge_mshr_on_writeback(PACKET& src, mshr_iterator dst),
        merge_mshr_on_prefetch(PACKET& src, mshr_iterator dst);
    bool mshr_full() const;

    virtual void reset_stats();

    virtual void report(std::ostream& os, const size_t& i);

    virtual void update_footprint(const uint64_t& addr,
                                  const footprint_bitmap& footprint) = 0;

    // bool prefetch_line ()
    bool prefetch_line(const uint32_t& cpu, const uint8_t& size,
                       const uint64_t& ip, const uint64_t& base_addr,
                       const uint64_t& pf_addr, const fill_levels& fill_level,
                       const uint32_t& metadata);
    bool prefetcher_enable(const uint64_t& base_addr) const;
    void set_prefetcher_psel_bits(const uint64_t& size),
        set_prefetcher_threshold(const uint64_t& threshold);
    void increment_pref_pred(const uint64_t& base_addr),
        decrement_pref_pred(const uint64_t& base_addr);

    // Interface with the outside world.
    virtual int add_rq(PACKET* packet) override;
    virtual int add_wq(PACKET* packet) override;
    virtual int add_pq(PACKET* packet) override;
    virtual void return_data(PACKET* packet) override;
    virtual void operate() override;
    virtual void increment_WQ_FULL(uint64_t address) override;
    virtual uint32_t get_occupancy(uint8_t queue_type,
                                   uint64_t address) override;
    virtual uint32_t get_size(uint8_t queue_type, uint64_t address) override;

    virtual int32_t add_read_queue(PACKET& packet) override;
    virtual int32_t add_write_queue(PACKET& packet) override;
    virtual int32_t add_prefetch_queue(PACKET& packet) override;

    virtual uint16_t get_way(const uint64_t& addr,
                             const uint32_t& set) const = 0;
    virtual uint32_t get_set(const uint64_t& addr) const = 0;
    virtual uint64_t get_paddr(const uint32_t& set,
                               const uint16_t& way) const = 0;

    virtual void invalidate_line(const uint64_t& full_addr) = 0;

    virtual bool is_valid(const uint32_t& set, const uint16_t& way) = 0;

    virtual void return_data(PACKET& packet) override = 0;
    virtual void return_data(PACKET& packet,
                             const boost::dynamic_bitset<>& valid_bits) = 0;

    virtual uint32_t read_queue_occupancy() override,
        read_queue_occupancy(const uint64_t& addr) override,
        write_queue_occupancy() override,
        write_queue_occupancy(const uint64_t& addr) override,
        prefetch_queue_occupancy() override,
        prefetch_queue_occupancy(const uint64_t& addr) override,
        read_queue_size() override,
        read_queue_size(const uint64_t& addr) override,
        write_queue_size() override,
        write_queue_size(const uint64_t& addr) override,
        prefetch_queue_size() override,
        prefetch_queue_size(const uint64_t& addr) override;

    uint32_t processed_queue_occupancy() const, processed_queue_size() const;

    std::size_t mshr_size() const, mshr_occupancy() const;

    virtual uint32_t block_size() const;
    uint32_t log2_block_size() const;

    PACKET_QUEUE *read_queue() const, *write_queue() const,
        *prefetch_queue() const, *processed_queue() const;

   protected:
    virtual void _init_cache_impl(const pt::ptree& props);

    virtual uint64_t _get_tag(const PACKET& packet) const = 0;

    virtual bool _should_slice_packet(const PACKET& packet) const = 0;
    virtual std::vector<PACKET> _slice_packet(const PACKET& packet) const = 0;

    virtual bool _is_hit(const PACKET& packet) const = 0;
    virtual bool _is_hit(const PACKET& packet, const uint32_t& set) const = 0;

    virtual hit_types __is_hit(const PACKET& packet) const = 0;
    virtual hit_types __is_hit(const PACKET& packet,
                               const uint32_t& set) const = 0;

    void _sort_mshr();
    virtual std::vector<PACKET>::iterator _add_mshr(const PACKET& packet) = 0;
    virtual void _fill_cache(const uint32_t& set, const uint16_t& way,
                             const PACKET& packet) = 0;
    virtual void _initialize_replacement_state() = 0;
    virtual void _update_replacement_state(
        helpers::cache_access_descriptor& desc) = 0;
    virtual uint32_t _find_victim(helpers::cache_access_descriptor& desc) = 0;
    virtual void _final_replacement_stats() = 0;

    virtual void _l1i_prefetcher_cache_operate();
    virtual void _l1d_prefetcher_operate(const uint32_t& cpu,
                                         const uint64_t& addr,
                                         const uint64_t& ip,
                                         const uint32_t& size, bool hit,
                                         const access_types& type);
    virtual void _l2c_prefetcher_operate(const uint32_t& cpu,
                                         const uint64_t& addr,
                                         const uint64_t& ip,
                                         const uint32_t& size, bool hit,
                                         const access_types& type);
    virtual void _llc_prefetcher_operate(const uint32_t& cpu,
                                         const uint64_t& addr,
                                         const uint64_t& ip,
                                         const uint32_t& size, bool hit,
                                         const access_types& type);
    virtual void _sdc_prefetcher_operate(const uint32_t& cpu,
                                         const uint64_t& addr,
                                         const uint64_t& ip,
                                         const uint32_t& size, bool hit,
                                         const access_types& type);
    virtual void _update_fill_path_lower_mshrs(mshr_iterator it) = 0;
    virtual void _update_pf_offchip_pred_lower(mshr_iterator it) = 0;

    // Working methods that should not be visible from the outside. This methods
    // are meant to handle the different accesses to the cache and eventually
    // modify internal structures when needed.
    virtual void _handle_fill() = 0;
    virtual void _handle_read() = 0;
    virtual void _handle_writeback() = 0;
    virtual void _handle_prefetch() = 0;

   private:
    void _account_non_aligned(const bool& aligned, uint64_t& non_aligned_cnt,
                              uint64_t& received_cnt);
};

cache::fill_levels next_fill_level(const cache::fill_levels& fl);
cache::fill_levels prev_fill_level(const cc::block_location& loc);
};  // namespace components
}  // namespace champsim

// Arithmetic operators on caches.
bool operator<(const cc::cache& lhs, const cc::cache& rhs);
bool operator>(const cc::cache& lhs, const cc::cache& rhs);

std::ostream& operator<<(std::ostream& os,
                         const champsim::components::cache& c);
std::ostream& operator<<(std::ostream& os,
                         const champsim::components::cache::cache_stats& cs);

#endif  // __CHAMPSIM_INTERNALS_COMPONENTS_CACHE_HH__
