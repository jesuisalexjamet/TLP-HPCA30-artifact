#include <plugins/prefetchers/l2c_next_line/l2c_next_line.hh>
#
#include <boost/property_tree/json_parser.hpp>

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::l2c_next_line_prefetcher::l2c_next_line_prefetcher() {}

cp::l2c_next_line_prefetcher::l2c_next_line_prefetcher(
    const cp::l2c_next_line_prefetcher& o)
    : iprefetcher(o) {}

/**
 * Destructor of the class.
 */
cp::l2c_next_line_prefetcher::~l2c_next_line_prefetcher() {}

/**
 * @brief This method performs the actual operation of the prefetcher. This
 * method is meant to be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch
 * request.
 */
void cp::l2c_next_line_prefetcher::operate(
    const cp::prefetch_request_descriptor& desc) {
    for (uint64_t i = 1; i <= this->_degree; i++) {
        uint64_t pf_addr = ((desc.addr >> LOG2_BLOCK_SIZE) + i)
                           << LOG2_BLOCK_SIZE;

        this->_cache_inst->prefetch_line(desc.cpu, BLOCK_SIZE, desc.ip,
                                         desc.addr, pf_addr, cc::cache::fill_l2,
                                         0);
    }
}

cp::l2c_next_line_prefetcher* cp::l2c_next_line_prefetcher::clone() {
    return new l2c_next_line_prefetcher(*this);
}

/**
 * This method is used to create an instance of the prefetcher and provide it to
 * the performance model.
 */
cp::iprefetcher* cp::l2c_next_line_prefetcher::create_prefetcher() {
    return new cp::l2c_next_line_prefetcher();
}

/**
 * @brief Initializes the L2C Next Line prefetcher based on the content of a
 * configuration file.
 *
 * @param props An object containing the configuration of the prefetcher.
 * @param cache_inst The instance of the cache to which the prefetcher is
 * attached.
 */
void cp::l2c_next_line_prefetcher::_init(const pt::ptree& props,
                                         cc::cache* cache_inst) {
    // Calling the version of the parent class first.
    cp::iprefetcher::_init(props, cache_inst);

    this->_degree = props.get<uint64_t>("degree");
}
