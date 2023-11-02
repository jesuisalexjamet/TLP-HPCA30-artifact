#include <plugins/prefetchers/sdc_next_line/sdc_next_line.hh>
#
#include <boost/property_tree/json_parser.hpp>

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::sdc_next_line_prefetcher::sdc_next_line_prefetcher () {

}

cp::sdc_next_line_prefetcher::sdc_next_line_prefetcher (const cp::sdc_next_line_prefetcher& o) :
    iprefetcher (o) {

}

/**
 * Destructor of the class.
 */
cp::sdc_next_line_prefetcher::~sdc_next_line_prefetcher () {

}

/**
 * @brief This method performs the actual operation of the prefetcher. This method is meant to
 * be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch request.
 */
void cp::sdc_next_line_prefetcher::operate (const cp::prefetch_request_descriptor& desc) {
	uint64_t pf_addr = ((desc.addr >> this->_cache_inst->log2_block_size ()) + 1) << this->_cache_inst->log2_block_size ();

    this->_cache_inst->prefetch_line (desc.cpu, this->_cache_inst->block_size (), desc.ip, desc.addr, pf_addr, cc::cache::fill_sdc , 0);
}

cp::sdc_next_line_prefetcher* cp::sdc_next_line_prefetcher::clone () {
	return new sdc_next_line_prefetcher (*this);
}

/**
 * This method is used to create an instance of the prefetcher and provide it to the performance model.
 */
cp::iprefetcher* cp::sdc_next_line_prefetcher::create_prefetcher () {
	return new cp::sdc_next_line_prefetcher ();
}
