#include <plugins/prefetchers/l2c_no/l2c_no.hh>
#
#include <boost/property_tree/json_parser.hpp>

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::l2c_no_prefetcher::l2c_no_prefetcher () {

}

cp::l2c_no_prefetcher::l2c_no_prefetcher (const cp::l2c_no_prefetcher& o) :
    iprefetcher (o) {

}

/**
 * Destructor of the class.
 */
cp::l2c_no_prefetcher::~l2c_no_prefetcher () {

}

/**
 * @brief This method performs the actual operation of the prefetcher. This method is meant to
 * be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch request.
 */
void cp::l2c_no_prefetcher::operate (const cp::prefetch_request_descriptor& desc) {
	// Nothing is done here.
}

cp::l2c_no_prefetcher* cp::l2c_no_prefetcher::clone () {
	return new l2c_no_prefetcher (*this);
}

/**
 * This method is used to create an instance of the prefetcher and provide it to the performance model.
 */
cp::l2c_no_prefetcher* cp::l2c_no_prefetcher::create_prefetcher () {
	return new cp::l2c_no_prefetcher ();
}
