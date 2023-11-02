#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_MEMORY_SYSTEM_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_MEMORY_SYSTEM_HH__

#include <internals/components/memory_enums.hh>
#
#include <internals/block.h>

// Forward declaration.
class MEMORY_CONTROLLER;

namespace champsim {
namespace components {
class memory_system {
   protected:
    // Other caches or memory devices interfacing with this cache.
    std::vector<memory_system*> _upper_level_icache, _upper_level_dcache,
        _upper_level_irreg;
    memory_system* _lower_level_memory;
    MEMORY_CONTROLLER* _dram;

   public:
    memory_system();
    virtual ~memory_system();

    void set_lower_level_memory(memory_system* ms);
    void add_upper_level_icache(memory_system* ms);
    void set_upper_level_icache(const std::vector<memory_system*>& uli);
    void add_upper_level_dcache(memory_system* ms);
    void set_upper_level_dcache(const std::vector<memory_system*>& uld);
    void add_upper_level_irreg_cache(memory_system* ms);
    void set_upper_level_irreg_cache(const std::vector<memory_system*>& irreg);

    void set_dram(MEMORY_CONTROLLER* dram);

    std::list<memory_system*> lower_levels() const;

    // Interface with the outside world.
    virtual int32_t add_read_queue(PACKET& packet) = 0;
    virtual int32_t add_write_queue(PACKET& packet) = 0;
    virtual int32_t add_prefetch_queue(PACKET& packet) = 0;

    virtual void return_data(PACKET& packet) = 0;
    virtual void return_data(PACKET& packet,
                             const boost::dynamic_bitset<>& valid_bits) = 0;

    virtual uint32_t read_queue_occupancy() = 0,
                     read_queue_occupancy(const uint64_t&addr) = 0,
                     write_queue_occupancy() = 0,
                     write_queue_occupancy(const uint64_t&addr) = 0,
                     prefetch_queue_occupancy() = 0,
                     prefetch_queue_occupancy(const uint64_t&addr) = 0,
                     read_queue_size() = 0,
                     read_queue_size(const uint64_t&addr) = 0,
                     write_queue_size() = 0,
                     write_queue_size(const uint64_t&addr) = 0,
                     prefetch_queue_size() = 0,
                     prefetch_queue_size(const uint64_t&addr) = 0;
};
}  // namespace components
}  // namespace champsim

#endif  // __CHAMPSIM_INTERNALS_COMPONENTS_MEMORY_SYSTEM_HH__
