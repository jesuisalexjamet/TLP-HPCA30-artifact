#ifndef __CHAMPSIM_INTERNALS_POLICIES_FILL_PATH_POLICIES_HH__
#define __CHAMPSIM_INTERNALS_POLICIES_FILL_PATH_POLICIES_HH__

#include <internals/block.h>
#
#include <internals/dram_controller.h>

#include <internals/components/cache.hh>
#include <internals/prefetchers/iprefetcher.hh>

namespace cp = champsim::prefetchers;

namespace champsim::policies {
class abstract_fill_path_policy {
   private:
    std::size_t _cpu_idx;

   protected:
    cc::cache *_l1i, *_l1d, *_l2c, *_llc;
    MEMORY_CONTROLLER* _dram;

   protected:
    void _prefetch_on_higher_prefetch(
        cc::cache* c, const PACKET& packet,
        const cp::prefetch_request_descriptor& p_desc);
    virtual bool _propagate_l1i_miss(PACKET& packet);
    virtual bool _propagate_l1d_miss(PACKET& packet) = 0;
    virtual bool _propagate_l2c_miss(PACKET& packet) = 0;
    virtual bool _propagate_llc_miss(PACKET& packet) = 0;

   public:
    abstract_fill_path_policy(const std::size_t& cpu_idx);
    virtual ~abstract_fill_path_policy();

    bool propagate_miss(cc::cache* c, PACKET& packet);
    virtual void prefetch_on_higher_prefetch_on_hit(cc::cache*c, const PACKET& packet),
        prefetch_on_higher_prefetch_on_miss(cc::cache*c, const PACKET& packet);

    static cc::sdc_routes route(const cc::block_location& loc);
};

class conservative_fill_path_policy : public abstract_fill_path_policy {
   private:
    bool _propagate_l1d_l2c_route(PACKET& packet);
    bool _propagate_l1d_llc_route(PACKET& packet);
    bool _propagate_l1d_dram_route(PACKET& packet);

   protected:
    virtual bool _propagate_l1d_miss(PACKET& packet) override;
    virtual bool _propagate_l2c_miss(PACKET& packet) override;
    virtual bool _propagate_llc_miss(PACKET& packet) override;

   public:
    conservative_fill_path_policy(const std::size_t& cpu_idx);
    virtual ~conservative_fill_path_policy();
};
}  // namespace champsim::policies

#endif  // __CHAMPSIM_INTERNALS_POLICIES_FILL_PATH_POLICIES_HH__