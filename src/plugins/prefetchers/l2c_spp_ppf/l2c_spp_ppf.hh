#ifndef __CHAMPSIM_PLUGINS_PREFETCHERS_L2C_SPP_PPF_HH__
#define __CHAMPSIM_PLUGINS_PREFETCHERS_L2C_SPP_PPF_HH__

#include <internals/prefetchers/iprefetcher.hh>
#
#include <plugins/prefetchers/l2c_spp_ppf/ppf.h>
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
 * @brief An implementation of a L2C prefetcher that does nothing.
 */
class l2c_spp_ppf_prefetcher : public iprefetcher {
   public:
    virtual ~l2c_spp_ppf_prefetcher();

    virtual void operate(const prefetch_request_descriptor& desc) final;
    virtual void fill(
        const champsim::helpers::cache_access_descriptor& desc) final;

    virtual l2c_spp_ppf_prefetcher* clone() final;
    virtual void clone(l2c_spp_ppf_prefetcher* o) final;

    static iprefetcher* create_prefetcher();

   protected:
    l2c_spp_ppf_prefetcher();

   private:
    l2c_spp_ppf_prefetcher(const l2c_spp_ppf_prefetcher& o);

    virtual void _init(const pt::ptree& props, cc::cache* cache_inst) final;

   private:
    int _depth_track[30];
    int _prefetch_q_full;
};
}  // namespace prefetchers
}  // namespace champsim

// Exporting the symbol used for module loading.
BOOST_DLL_ALIAS(
    champsim::prefetchers::l2c_spp_ppf_prefetcher::create_prefetcher,
    create_prefetcher)

#endif  // __CHAMPSIM_PLUGINS_PREFETCHERS_L2C_SPP_PPF_HH__
