#include <algorithm>
#
#include <internals/components/cache.hh>
#
#include <plugins/replacements/llc_drrip/llc_drrip.hh>

namespace cc = champsim::components;
namespace cr = champsim::replacements;

/**
 * @brief Constructor of the class.
 */
cr::llc_drrip_replacement_policy::llc_drrip_replacement_policy () {

}

/**
 * @brief Destructor of the class.
 */
cr::llc_drrip_replacement_policy::~llc_drrip_replacement_policy () {

}

void cr::llc_drrip_replacement_policy::update_replacement_state (const ch::cache_access_descriptor& desc) {

}

std::size_t cr::llc_drrip_replacement_policy::find_victim (const ch::cache_access_descriptor& desc) {

}

/**
 * @brief This method is used to create an instance of the prefetcher and provide
 * it to the performance model.
 */
cr::ireplacementpolicy* cr::llc_drrip_replacement_policy::create_replacementpolicy () {
	return new cr::llc_drrip_replacement_policy ();
}

void cr::llc_drrip_replacement_policy::_init (const pt::ptree& props, cc::cache* cache_inst) {
	cr::ireplacementpolicy::_init (props, cache_inst);
}
