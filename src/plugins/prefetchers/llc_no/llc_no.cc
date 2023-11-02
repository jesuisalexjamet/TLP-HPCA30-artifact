#include <plugins/prefetchers/llc_no/llc_no.hh>
#
#include <boost/property_tree/json_parser.hpp>

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::llc_no_prefetcher::llc_no_prefetcher () {

}

cp::llc_no_prefetcher::llc_no_prefetcher (const cp::llc_no_prefetcher& o) :
    iprefetcher (o) {

}

/**
 * Destructor of the class.
 */
cp::llc_no_prefetcher::~llc_no_prefetcher () {

}

/**
 * @brief This method performs the actual operation of the prefetcher. This method is meant to
 * be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch request.
 */
void cp::llc_no_prefetcher::operate (const cp::prefetch_request_descriptor& desc) {
	// Nothing is done here.
}

cp::llc_no_prefetcher* cp::llc_no_prefetcher::clone () {
	return new llc_no_prefetcher (*this);
}

/**
 * This method is used to create an instance of the prefetcher and provide it to the performance model.
 */
cp::iprefetcher* cp::llc_no_prefetcher::create_prefetcher () {
	return new cp::llc_no_prefetcher ();
}
