#ifndef __CHAMPSIM_PLUGINS_PREFETCHERS_L1D_BERTI_HH__
#define __CHAMPSIM_PLUGINS_PREFETCHERS_L1D_BERTI_HH__

#include <internals/prefetchers/iprefetcher.hh>
#
#include <boost/shared_ptr.hpp>
#
#include <boost/property_tree/ptree.hpp>
#
#include <boost/dll.hpp>
#include <boost/dll/alias.hpp>

namespace dll = boost::dll;
namespace pt = boost::property_tree;

namespace champsim {
namespace prefetchers {
/**
 * @brief An implementation of a next line L1D prefetcher.
 */
class l1d_berti : public iprefetcher {
   private:
    struct current_page_entry {
        uint64_t page_addr, u_vector, lru;
        std::vector<int> berti;
        std::vector<unsigned int> berti_score;
        int current_berti, stride;
        bool short_reuse, continue_burst;
    };

    using current_page_table = std::vector<current_page_entry>;

    struct previous_request_entry {
        uint64_t page_addr_pointer, offset, time;
    };

    using previous_request_table = std::vector<previous_request_entry>;

    struct latency_entry {
        uint64_t page_addr_pointer, offset, time_lat;
        bool completed;
    };

    using latency_table = std::vector<latency_entry>;

    struct record_page_entry {
        uint64_t page_addr, linnea, last_offset, lru;
        bool short_reuse;
    };

    using record_page_table = std::vector<record_page_entry>;

    struct ip_entry {
        bool current;
        int berti_or_pointer;
        bool consecutive, short_reuse;
    };

    using ip_table = std::vector<ip_entry>;

   public:
    struct berti_stats {
       public:
        std::vector<uint64_t> l1d_ip_misses, l1d_ip_hits, l1d_ip_late,
            l1d_ip_early;
        uint64_t stats_pref_addr, stats_pref_ip, stats_pref_current,
            cache_accesses, cache_misses;

       public:
        berti_stats() = default;
        berti_stats(const std::size_t& ip_table_entries) {}
    };

   public:
    virtual ~l1d_berti();

    virtual void operate(const prefetch_request_descriptor& desc) final;
    virtual void fill(
        const champsim::helpers::cache_access_descriptor& desc) final;

    virtual void clear_stats() final, dump_stats() final;

    virtual l1d_berti* clone() final;

    static iprefetcher* create_prefetcher();

   protected:
    l1d_berti();

   private:
    l1d_berti(const l1d_berti& o);

    virtual void _init(const pt::ptree& props, cc::cache* cache_inst) final;

    // Prefetcher's statistics
    berti_stats _stats;
};
}  // namespace prefetchers
}  // namespace champsim

std::ostream& operator<<(
    std::ostream& os, const champsim::prefetchers::l1d_berti::berti_stats& bs);

// Exporting the symbol used for module loading.
BOOST_DLL_ALIAS(champsim::prefetchers::l1d_berti::create_prefetcher,
                create_prefetcher)

#endif  // __CHAMPSIM_PLUGINS_PREFETCHERS_L1D_BERTI_HH__
