#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_MEMORY_ENUMS_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_MEMORY_ENUMS_HH__

#include <cstdint>

namespace champsim {
namespace components {
enum cache_type : int32_t {
    is_itlb = 0x02,
    is_dtlb = 0x04,
    is_stlb = 0x08,
    is_l1i = 0x10,
    is_l1d = 0x20,
    is_l2c = 0x40,
    is_llc = 0x80,
    is_dram = 0x100,

    is_sdc = 0x200,

    is_loc = 0x0,
    is_woc = 0x1,
};

/**
 * @brief Defines the location of a block in the memory hierarchy.
 */
enum block_location : uint8_t {
#if defined(ENABLE_DCLR)
    is_in_l1d = 0x0,
    is_in_l2c = 0x1,  /*!< The block is located in the L2C. */
    is_in_llc = 0x2,  /*!< The block is located in the LLC. */
    is_in_dram = 0x4, /*!< The block is located in the DRAM. */
#else
    is_in_dram = 0x0, /*!< The block is located in the DRAM. */
    is_in_l2c = 0x1,  /*!< The block is located in the L2C. */
    is_in_llc = 0x2,  /*!< The block is located in the LLC. */
#endif  // ENABLE_DCLR

    // Compound enum value.
    is_in_both =
        is_in_l2c |
        is_in_llc /*!< The block is located in both the L2C and the LLC. */
};

bool location_match(const block_location& l, const block_location& r),
     offchip_match(const block_location& l, const block_location& r);

enum hit_types : int32_t {
    loc_hit = 0,
    woc_hit = 1,
    hole_miss = 2,
    line_miss = 3,
};

enum sdc_routes {
    sdc_l2c_dram = 0x1,
    sdc_llc_dram = 0x2,
    sdc_dram = 0x4,

    l1d_dram = 0x8,
    l1d_llc = 0x10,

    dram_ddrp_request = 0x20,

    invalid_route = 0x0,
};
}  // namespace components
}  // namespace champsim

#endif  // __CHAMPSIM_INTERNALS_COMPONENTS_MEMORY_ENUMS_HH__
