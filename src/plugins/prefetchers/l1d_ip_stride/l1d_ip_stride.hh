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
class l1d_ip_stride_prefetcher : public iprefetcher {
   private:
    struct ip_tracker {
       public:
        uint64_t ip,         /*!< The IP we are tracking. */
            last_cl_addr;    /*!< The latest address accessed by this IP. */
        int64_t last_stride; /*!< The stride between the last two addresses
                                accessed by this IP. */
        uint32_t lru;        /*!< Use LRU to evict old IP trackers. */

       public:
        ip_tracker() : ip(0ULL), last_cl_addr(0ULL), last_stride(0LL), lru(0) {}

        bool operator==(const uint64_t& ip) { return this->ip == ip; }
    };

    using tracker_array = std::vector<ip_tracker>;

   public:
    virtual ~l1d_ip_stride_prefetcher();

    virtual void operate(const prefetch_request_descriptor& desc) final;

    virtual l1d_ip_stride_prefetcher* clone() final;

    static iprefetcher* create_prefetcher();

   protected:
    l1d_ip_stride_prefetcher();

   private:
    l1d_ip_stride_prefetcher(const l1d_ip_stride_prefetcher& o);

    virtual void _init(const pt::ptree& props, cc::cache* cache_inst) final;

    tracker_array::iterator _find_victim();
    tracker_array::iterator _lookup_trackers(const uint64_t& ip);
    void _update_replacement_state(tracker_array::iterator it);

   private:
    uint64_t _prefetch_degree, _ip_tracker_size;
    std::vector<ip_tracker> _trackers;
};
}  // namespace prefetchers
}  // namespace champsim

// Exporting the symbol used for module loading.
BOOST_DLL_ALIAS(
    champsim::prefetchers::l1d_ip_stride_prefetcher::create_prefetcher,
    create_prefetcher)
