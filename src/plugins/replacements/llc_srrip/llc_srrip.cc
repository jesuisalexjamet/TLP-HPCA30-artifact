#include <algorithm>
#
#include <internals/components/cache.hh>
#
#include <plugins/replacements/llc_srrip/llc_srrip.hh>

#define maxRRPV 3

namespace cc = champsim::components;
namespace cr = champsim::replacements;

/**
 * @brief Constructor of the class.
 */
cr::llc_srrip_replacement_policy::llc_srrip_replacement_policy () {

}

/**
 * @brief Destructor of the class.
 */
cr::llc_srrip_replacement_policy::~llc_srrip_replacement_policy () {

}

void cr::llc_srrip_replacement_policy::update_replacement_state (const ch::cache_access_descriptor& desc) {
	// if (desc.hit && desc.type == cc::cache::writeback) {
	// 	return;
	// }

	if (desc.hit) {
		this->_repl[desc.set][desc.way] = 0;
	} else {
		this->_repl[desc.set][desc.way] = maxRRPV - 1;
	}
}

std::size_t cr::llc_srrip_replacement_policy::find_victim (const ch::cache_access_descriptor& desc) {
	while (true) {
		for (std::size_t i = 0; i < this->_cache_inst->associativity (); i++) {
			if (this->_repl[desc.set][i] == maxRRPV) {
				return i;
			}
		}

		for (std::size_t i = 0; i < this->_cache_inst->associativity (); i++) {
			this->_repl[desc.set][i]++;
		}
	}
}

/**
 * @brief This method is used to create an instance of the prefetcher and provide
 * it to the performance model.
 */
cr::ireplacementpolicy* cr::llc_srrip_replacement_policy::create_replacementpolicy () {
	return new cr::llc_srrip_replacement_policy ();
}

void cr::llc_srrip_replacement_policy::_init (const pt::ptree& props, cc::cache* cache_inst) {
	cr::ireplacementpolicy::_init (props, cache_inst);

	// Initializing the replacement states.
	this->_repl = replacement_state_array (cache_inst->sets (), replacement_state_set_array (cache_inst->associativity (), maxRRPV));
}
