#include <algorithm>
#
#include <internals/components/cache.hh>
#
#include <plugins/replacements/l1i_lru/l1i_lru.hh>

namespace cc = champsim::components;
namespace cr = champsim::replacements;

/**
 * @brief Constructor of the class.
 */
cr::l1i_lru_replacement_policy::l1i_lru_replacement_policy () {

}

/**
 * @brief Destructor of the class.
 */
cr::l1i_lru_replacement_policy::~l1i_lru_replacement_policy () {

}

void cr::l1i_lru_replacement_policy::update_replacement_state (const ch::cache_access_descriptor& desc) {
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

std::size_t cr::l1i_lru_replacement_policy::find_victim (const ch::cache_access_descriptor& desc) {
	std::size_t way = std::distance (this->_repl[desc.set].begin (), std::max_element (this->_repl[desc.set].begin (), this->_repl[desc.set].end (),
		[] (const uint8_t& a, const uint8_t& b) -> bool {
			return (a < b);
		}));

	return way;
}

/**
 * @brief This method is used to create an instance of the prefetcher and provide
 * it to the performance model.
 */
cr::ireplacementpolicy* cr::l1i_lru_replacement_policy::create_replacementpolicy () {
	return new cr::l1i_lru_replacement_policy ();
}

void cr::l1i_lru_replacement_policy::_init (const pt::ptree& props, cc::cache* cache_inst) {
	cr::ireplacementpolicy::_init (props, cache_inst);

	// Initializing the replacement states.
	this->_repl = replacement_state_array (cache_inst->sets (), replacement_state_set_array (cache_inst->associativity (), cache_inst->associativity () - 1));
}
