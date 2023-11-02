#include <plugins/prefetchers/l1d_next_line/l1d_next_line.hh>
#
#include <boost/property_tree/json_parser.hpp>

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::l1d_next_line_prefetcher::l1d_next_line_prefetcher () {

}

cp::l1d_next_line_prefetcher::l1d_next_line_prefetcher (const cp::l1d_next_line_prefetcher& o) :
    iprefetcher (o) {

}

/**
 * Destructor of the class.
 */
cp::l1d_next_line_prefetcher::~l1d_next_line_prefetcher () {

}

/**
 * @brief This method performs the actual operation of the prefetcher. This method is meant to
 * be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch request.
 */
void cp::l1d_next_line_prefetcher::operate (const cp::prefetch_request_descriptor& desc) {
	uint64_t pf_addr = ((desc.addr >> LOG2_BLOCK_SIZE) + 1) << LOG2_BLOCK_SIZE;

    this->_cache_inst->prefetch_line (desc.cpu, BLOCK_SIZE, desc.ip, desc.addr, pf_addr, cc::cache::fill_l1 , 0);
}

cp::l1d_next_line_prefetcher* cp::l1d_next_line_prefetcher::clone () {
	return new l1d_next_line_prefetcher (*this);
}

/**
 * This method is used to create an instance of the prefetcher and provide it to the performance model.
 */
cp::iprefetcher* cp::l1d_next_line_prefetcher::create_prefetcher () {
	return new cp::l1d_next_line_prefetcher ();
}
