#include <plugins/prefetchers/l1d_no/l1d_no.hh>

namespace cp = champsim::prefetchers;

/**
 * @brief Constructor of the class.
 */
cp::l1d_no_prefetcher::l1d_no_prefetcher () {

}

cp::l1d_no_prefetcher::l1d_no_prefetcher (const cp::l1d_no_prefetcher& o) :
    iprefetcher (o) {

}

/**
 * @brief Destructor of the class.
 */
cp::l1d_no_prefetcher::~l1d_no_prefetcher () {

}

/**
 * @brief This method performs the actual operation of the prefetcher. This method is meant to
 * be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch request.
 */
void cp::l1d_no_prefetcher::operate (const cp::prefetch_request_descriptor& desc) {
	// Nothing is done here.
}

cp::l1d_no_prefetcher* cp::l1d_no_prefetcher::clone () {
	return new l1d_no_prefetcher (*this);
}

/**
 * @brief This method is used to create an instance of the prefetcher and provide
 * it to the performance model.
 */
cp::iprefetcher* cp::l1d_no_prefetcher::create_prefetcher () {
	return new cp::l1d_no_prefetcher ();
}
