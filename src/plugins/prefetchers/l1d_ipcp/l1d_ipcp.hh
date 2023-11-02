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
class l1d_ipcp : public iprefetcher {
   private:
    struct ip_tracker_entry {
       public:
        bool ip_valid, str_valid, str_dir, str_strength;
        uint16_t signature;
        uint64_t ip_tag, last_page, last_cl_offset;
        int64_t last_stride;
        int conf;

       public:
        ip_tracker_entry()
            : ip_valid(false),
              str_valid(false),
              str_dir(false),
              signature(0),
              str_strength(0),
              ip_tag(0),
              last_page(0),
              last_cl_offset(0),
              last_stride(0),
              conf(0) {}
    };

    struct delta_prediction_entry {
       public:
        int delta, conf;

       public:
        delta_prediction_entry() : delta(0), conf(0) {}
    };

    struct ipcp_stats {
       public:
        uint64_t misses, /*!< Number of misses in the IP tracker table. */
            cs,          /*!< Number of constant stride detected. */
            cplx,        /*!< Number of complex stride detected. */
            str,         /*!< Number of stream detected. */
            nl; /*!< Number of next line fallback (this is a default behaviour).
                 */

       public:
        ipcp_stats() : misses(0), cs(0), cplx(0), str(0) {}
    };

    using ip_tracker = std::vector<ip_tracker_entry>;
    using delta_prediction_table = std::vector<delta_prediction_entry>;

   public:
    virtual ~l1d_ipcp();

    virtual void operate(const prefetch_request_descriptor& desc) final;

    virtual void clear_stats() final, dump_stats() final;

    virtual l1d_ipcp* clone() final;

    static iprefetcher* create_prefetcher();

   protected:
    l1d_ipcp();

   private:
    l1d_ipcp(const l1d_ipcp& o);

    virtual void _init(const pt::ptree& props, cc::cache* cache_inst) final;

    uint16_t _compute_signature(const uint16_t& old_sig, int delta);
    void _check_for_stream(const uint64_t& index, const uint64_t& cl_addr);
    void _update_confidence(int& confidence, const int& stride,
                            const int& pred_stride);

   private:
    uint64_t _ip_table_size, _ghb_size, _ip_index_bits, _ip_tag_bits;

    // Prefetcher's data structures.
    ip_tracker _trackers;
    delta_prediction_table _dpt;
    std::vector<uint64_t> _ghb;
    uint64_t _num_misses, _prev_cpu_cycle;
    float _mpkc;
    int _spec_nl;

    // Prefetcher's statistics
    ipcp_stats _stats;
};
}  // namespace prefetchers
}  // namespace champsim

// Exporting the symbol used for module loading.
BOOST_DLL_ALIAS(champsim::prefetchers::l1d_ipcp::create_prefetcher,
                create_prefetcher)
