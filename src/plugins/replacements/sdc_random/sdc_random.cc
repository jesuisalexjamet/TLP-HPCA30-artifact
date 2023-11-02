#include <algorithm>
#
#include <internals/components/cache.hh>
#
#include <plugins/replacements/sdc_random/sdc_random.hh>

namespace cc = champsim::components;
namespace cr = champsim::replacements;

/**
 * @brief Constructor of the class.
 */
cr::sdc_random_replacement_policy::sdc_random_replacement_policy () {

}

/**
 * @brief Destructor of the class.
 */
cr::sdc_random_replacement_policy::~sdc_random_replacement_policy () {

}

void cr::sdc_random_replacement_policy::report (std::ostream& os) {

}

void cr::sdc_random_replacement_policy::update_replacement_state (const ch::cache_access_descriptor& desc) {

}

std::size_t cr::sdc_random_replacement_policy::find_victim (const ch::cache_access_descriptor& desc) {
	return this->_dist (this->_gen);
}

/**
 * @brief This method is used to create an instance of the prefetcher and provide
 * it to the performance model.
 */
cr::ireplacementpolicy* cr::sdc_random_replacement_policy::create_replacementpolicy () {
	return new cr::sdc_random_replacement_policy ();
}

void cr::sdc_random_replacement_policy::_init (const pt::ptree& props, cc::cache* cache_inst) {
	cr::ireplacementpolicy::_init (props, cache_inst);

	this->_dist = std::uniform_int_distribution<> (0, cache_inst->associativity () - 1);
}
