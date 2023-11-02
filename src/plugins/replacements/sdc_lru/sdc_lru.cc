#include <algorithm>
#
#include <internals/components/cache.hh>
#
#include <plugins/replacements/sdc_lru/sdc_lru.hh>

namespace cc = champsim::components;
namespace cr = champsim::replacements;

/**
 * @brief Constructor of the class.
 */
cr::sdc_lru_replacement_policy::sdc_lru_replacement_policy () {

}

/**
 * @brief Destructor of the class.
 */
cr::sdc_lru_replacement_policy::~sdc_lru_replacement_policy () {

}

void cr::sdc_lru_replacement_policy::report (std::ostream& os) {

}

void cr::sdc_lru_replacement_policy::update_replacement_state (const ch::cache_access_descriptor& desc) {
	if (desc.hit && desc.type == cc::cache::writeback) {
		return;
	}

	std::for_each (this->_repl[desc.set].begin (), this->_repl[desc.set].end (),
					[touch_lru = this->_repl[desc.set][desc.way]] (uint8_t& e) -> void {
						if (e < touch_lru) {
							e++;
						}
					});

	this->_repl[desc.set][desc.way] = 0x0;
}

std::size_t cr::sdc_lru_replacement_policy::find_victim (const ch::cache_access_descriptor& desc) {
	std::size_t way = this->_cache_inst->associativity ();

	way = std::distance (this->_repl[desc.set].begin (), std::max_element (this->_repl[desc.set].begin (), this->_repl[desc.set].end (),
		[] (const uint8_t& a, const uint8_t& b) -> bool {
			return (a < b);
		}));

	return way;
}

/**
 * @brief This method is used to create an instance of the prefetcher and provide
 * it to the performance model.
 */
cr::ireplacementpolicy* cr::sdc_lru_replacement_policy::create_replacementpolicy () {
	return new cr::sdc_lru_replacement_policy ();
}

void cr::sdc_lru_replacement_policy::_init (const pt::ptree& props, cc::cache* cache_inst) {
	cr::ireplacementpolicy::_init (props, cache_inst);

	// Initializing the replacement states.
	this->_repl = replacement_state_array (cache_inst->sets (), replacement_state_set_array (cache_inst->associativity (), cache_inst->associativity () - 1));
}
