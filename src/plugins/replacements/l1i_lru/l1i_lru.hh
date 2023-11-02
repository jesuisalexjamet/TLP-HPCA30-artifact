#include <internals/replacements/ireplacementpolicy.hh>
#
#include <cstdint>
#include <vector>
#
#include <boost/property_tree/ptree.hpp>
#
#include <boost/dll.hpp>
#include <boost/dll/alias.hpp>

namespace dll = boost::dll;
namespace pt = boost::property_tree;

namespace champsim {
	namespace replacements {
	/**
     * @brief An implementation of a L1D prefetcher that does nothing.
     */
    class l1i_lru_replacement_policy : public ireplacementpolicy {
		public:
			l1i_lru_replacement_policy ();
			virtual ~l1i_lru_replacement_policy ();

			virtual void update_replacement_state (const ch::cache_access_descriptor& desc) final;

			virtual std::size_t find_victim (const ch::cache_access_descriptor& desc) final;

			static ireplacementpolicy* create_replacementpolicy ();

		protected:
			void _init (const pt::ptree& props, cc::cache* cache_inst) final;

		private:
			using replacement_state_set_array = std::vector<uint8_t>;
			using replacement_state_array = std::vector<replacement_state_set_array>;

			replacement_state_array _repl;
		};

    // Exporting the symbol used for module loading.
		BOOST_DLL_ALIAS(
			champsim::replacements::l1i_lru_replacement_policy::create_replacementpolicy,
			create_replacementpolicy
		)
	}
}
